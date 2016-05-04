/*
 * (C) Copyright 2013 Kurento (http://kurento.org/)
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * Create by Tao Ren <tao@swarmnyc.com> <tour.ren.gz@gmail.com>
 */
#define _XOPEN_SOURCE 500

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "kmsepisodeoverlay.h"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>
#include <glib/gstdio.h>
#include <ftw.h>
#include <string.h>
#include <errno.h>

#include <opencv/cv.h>

#include <opencv/highgui.h>
#include <libsoup/soup.h>

#include <stdlib.h>
#include <glib-object.h>
#include <json-glib/json-glib.h>
#include <pango/pangocairo.h>

#define TEMP_PATH "/tmp/XXXXXX"
#define BLUE_COLOR (cvScalar (255, 0, 0, 0))
#define SRC_OVERLAY ((double)1)
#define MINIMUM_OUTLINE_OFFSET 1.0

#define PLUGIN_NAME "episodeoverlay"

GST_DEBUG_CATEGORY_STATIC (kms_episode_overlay_debug_category);
#define GST_CAT_DEFAULT kms_episode_overlay_debug_category

#define KMS_EPISODE_OVERLAY_LOCK(overlay) \
  (g_rec_mutex_lock (&( (KmsEpisodeOverlay *) overlay)->priv->mutex))

#define KMS_EPISODE_OVERLAY_UNLOCK(overlay) \
  (g_rec_mutex_unlock (&( (KmsEpisodeOverlay *) overlay)->priv->mutex))

#define KMS_EPISODE_OVERLAY_GET_PRIVATE(obj) ( \
  G_TYPE_INSTANCE_GET_PRIVATE (              \
    (obj),                                   \
    KMS_TYPE_EPISODE_OVERLAY,                  \
    KmsEpisodeOverlayPrivate                   \
  )                                          \
)

#define DEFAULT_STYLE NULL

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
# define CAIRO_ARGB_A 3
# define CAIRO_ARGB_R 2
# define CAIRO_ARGB_G 1
# define CAIRO_ARGB_B 0
#else
# define CAIRO_ARGB_A 0
# define CAIRO_ARGB_R 1
# define CAIRO_ARGB_G 2
# define CAIRO_ARGB_B 3
#endif

#define CAIRO_UNPREMULTIPLY(a,r,g,b) G_STMT_START { \
  b = (a > 0) ? MIN ((b * 255 + a / 2) / a, 255) : 0; \
  g = (a > 0) ? MIN ((g * 255 + a / 2) / a, 255) : 0; \
  r = (a > 0) ? MIN ((r * 255 + a / 2) / a, 255) : 0; \
} G_STMT_END

enum
{
  PROP_0,
  PROP_STYLE
};

#define MAX_VIEW_COUNT 4
#define MAX_TEXT_LENGTH 128
#define MSG_BAR_HEIGHT 30
#define MSG_BAR_BGCOLOR CV_RGB (73, 73, 73)
#define HOST_BAR_COLOR CV_RGB (255, 91, 0)
#define GUEST_BAR_COLOR CV_RGB (0, 103, 157)
#define MSG_BAR_ALPHA 0.85
typedef struct _KmsTextViewPrivate
{
  int x;
  int y;
  int width;
  int height;
  gchar text[MAX_TEXT_LENGTH];
  cairo_surface_t *textImage;
} KmsTextViewPrivate;

struct _KmsEpisodeOverlayPrivate
{
  GRecMutex mutex;
  IplImage *cvImage, *costume, *background, *background_image;
  GstStructure *image_to_overlay;

  gint output_width, output_height;
  gdouble offsetXPercent, offsetYPercent, widthPercent, heightPercent;
  gboolean show_debug_info, dir_created;
  gchar *dir;
  GstClockTime dts, pts;

  GQueue *events_queue;
  gchar *style;
  KmsTextViewPrivate views[MAX_VIEW_COUNT];
  gint enable;

  // for text font.
  PangoContext *pango_context;
  PangoLayout *layout;
  gdouble shadow_offset;
  gdouble outline_offset;
};

/* pad templates */

#define VIDEO_SRC_CAPS \
    GST_VIDEO_CAPS_MAKE("{ BGR }")

#define VIDEO_SINK_CAPS \
    GST_VIDEO_CAPS_MAKE("{ BGR }")

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (KmsEpisodeOverlay, kms_episode_overlay,
    GST_TYPE_VIDEO_FILTER,
    GST_DEBUG_CATEGORY_INIT (kms_episode_overlay_debug_category, PLUGIN_NAME,
        0, "debug category for episodeoverlay element"));

static gboolean
kms_episode_overlay_is_valid_uri (const gchar * url)
{
  gboolean ret;
  GRegex *regex;

  regex = g_regex_new ("^(?:((?:https?):)\\/\\/)([^:\\/\\s]+)(?::(\\d*))?(?:\\/"
      "([^\\s?#]+)?([?][^?#]*)?(#.*)?)?$", 0, 0, NULL);
  ret = g_regex_match (regex, url, G_REGEX_MATCH_ANCHORED, NULL);
  g_regex_unref (regex);

  return ret;
}

static void
load_from_url (gchar * file_name, const gchar * url)
{
  SoupSession *session;
  SoupMessage *msg;
  FILE *dst;

  session = soup_session_sync_new ();
  msg = soup_message_new ("GET", url);
  soup_session_send_message (session, msg);

  dst = fopen (file_name, "w+");

  if (dst == NULL) {
    GST_ERROR ("It is not possible to create the file");
    goto end;
  }
  fwrite (msg->response_body->data, 1, msg->response_body->length, dst);
  fclose (dst);

end:
  g_object_unref (msg);
  g_object_unref (session);
}

static void
kms_episode_overlay_adjust_values_with_fontdesc (KmsEpisodeOverlay * self,
    PangoFontDescription * desc)
{
  gint font_size = pango_font_description_get_size (desc) / PANGO_SCALE;

  self->priv->shadow_offset = (double) (font_size) / 13.0;
  self->priv->outline_offset = (double) (font_size) / 15.0;
  if (self->priv->outline_offset < MINIMUM_OUTLINE_OFFSET)
    self->priv->outline_offset = MINIMUM_OUTLINE_OFFSET;
}

static void
kms_episode_overlay_rebuild_text_images (KmsEpisodeOverlay * self)
{
  int i;
  KmsTextViewPrivate *data;

  for (i = 0; i < MAX_VIEW_COUNT; i++) {
    data = &self->priv->views[i];
    if (data->width > 0) {
      PangoRectangle ink_rect, logical_rect;
      gint width, height;
      cairo_t *cr;

      if (data->textImage != NULL) {
        cairo_surface_destroy (data->textImage);
        data->textImage = NULL;
      }
      if (strlen (data->text) == 0)
        continue;
      // draw text on pango layout.
      pango_layout_set_markup (self->priv->layout, data->text, -1);
      // calculate the size of text.
      pango_layout_get_pixel_extents (self->priv->layout, &ink_rect,
          &logical_rect);
      width = logical_rect.width + self->priv->shadow_offset;
      height = logical_rect.height + logical_rect.y + self->priv->shadow_offset;
      GST_INFO ("@rentao i=%d, text=%s width=%d height=%d", i, data->text,
          width, height);

      // draw on the cairo surface.
      data->textImage =
          cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
      cr = cairo_create (data->textImage);
      cairo_set_source_rgb (cr, 1.0, 1.0, 1.0); // white color to draw text.
      cairo_move_to (cr, 0.0, 0.0);
      pango_cairo_show_layout (cr, self->priv->layout);
      cairo_destroy (cr);
    }
  }
}

static gboolean
kms_episode_overlay_parse_style (KmsEpisodeOverlay * self)
{
  IplImage *backgroundImg = NULL;
  JsonParser *parser;
  GError *error;
  JsonReader *reader;
  gint width = 0, height = 0, x, y, count, i, disable;
  const gchar *text, *url;
  const gchar *fontdesc_str;

  parser = json_parser_new ();
  error = NULL;
  json_parser_load_from_data (parser, self->priv->style, -1, &error);
  if (error) {
    GST_INFO ("@rentao Unable to parse %s, err=%s", self->priv->style,
        error->message);
    g_error_free (error);
    g_object_unref (parser);
    return FALSE;
  }
  reader = json_reader_new (json_parser_get_root (parser));

  GST_INFO ("@rentao start parse(%s)", self->priv->style);

  // handle enable.
  if (json_reader_read_member (reader, "enable")) {
    self->priv->enable = json_reader_get_int_value (reader);
    GST_INFO ("@rentao set episodeoverlay enable=%d", self->priv->enable);
  }
  json_reader_end_element (reader);

  KMS_EPISODE_OVERLAY_LOCK (self);
  disable = self->priv->enable;
  self->priv->enable = 0;

  if (json_reader_read_member (reader, "width")) {
    width = json_reader_get_int_value (reader);
    if (width > 0) {
      self->priv->output_width = width;
      GST_INFO ("@rentao set output width to %d", width);
    }
  }
  json_reader_end_member (reader);

  if (json_reader_read_member (reader, "height")) {
    height = json_reader_get_int_value (reader);
    if (height > 0) {
      self->priv->output_height = height;
      GST_INFO ("@rentao set output height to %d", height);
    }
  }
  json_reader_end_member (reader);

  // handle background image.
  if (json_reader_read_member (reader, "background_image")) {
    url = json_reader_get_string_value (reader);
    if (!self->priv->dir_created) {
      gchar *d = g_strdup (TEMP_PATH);

      self->priv->dir = g_mkdtemp (d);
      self->priv->dir_created = TRUE;
      GST_INFO ("@rentao create tmp folder fot download image. %s", d);
    }

    backgroundImg =
        cvLoadImage (url, CV_LOAD_IMAGE_COLOR /*ignore alpha channel */ );

    if (backgroundImg != NULL) {
      GST_DEBUG ("Image loaded from file");
    } else {
      // load from url.
      if (kms_episode_overlay_is_valid_uri (url)) {
        gchar *file_name = g_strconcat (self->priv->dir, "/image.png", NULL);

        load_from_url (file_name, url);
        backgroundImg =
            cvLoadImage (file_name,
            CV_LOAD_IMAGE_COLOR /*ignore alpha channel */ );
        g_remove (file_name);
        g_free (file_name);
      }
    }

    if (self->priv->background_image != NULL) {
      cvReleaseImage (&self->priv->background_image);
      self->priv->background_image = NULL;
      // also reset the backgroud image.
    }
    if (backgroundImg == NULL) {
      GST_INFO ("@rentao Image not loaded from URL: %s", url);
    } else {
      GST_INFO ("@rentao Image loaded from URL: %s", url);
      self->priv->background_image = backgroundImg;
      if (self->priv->background != NULL) {
        cvReleaseImage (&self->priv->background);
        self->priv->background = NULL;
        GST_INFO ("@rentao reset background");
      }
    }
  }
  json_reader_end_element (reader);

  // handle font description
  if (json_reader_read_member (reader, "font-desc")) {
    PangoFontDescription *desc;

    fontdesc_str = json_reader_get_string_value (reader);
    desc = pango_font_description_from_string (fontdesc_str);
    if (desc) {
      GST_LOG_OBJECT (self, "font description set: %s", fontdesc_str);
      pango_layout_set_font_description (self->priv->layout, desc);
      kms_episode_overlay_adjust_values_with_fontdesc (self, desc);
      pango_font_description_free (desc);
    } else {
      GST_WARNING_OBJECT (self, "font description parse failed: %s",
          fontdesc_str);
    }
  }
  json_reader_end_element (reader);

  // handle views.
  if (json_reader_read_member (reader, "views")) {
    // got views, reset the original first,
    for (i = 0; i < MAX_VIEW_COUNT; i++) {
      self->priv->views[i].width = -1;
      self->priv->views[i].height = -1;
    }
    // also reset the backgroud image.
    if (self->priv->background != NULL) {
      cvReleaseImage (&self->priv->background);
      self->priv->background = NULL;
      GST_INFO ("@rentao reset background");
    }
    count = json_reader_count_elements (reader);
    count = (count < MAX_VIEW_COUNT) ? count : MAX_VIEW_COUNT;
    for (i = 0; i < count; i++) {
      json_reader_read_element (reader, i);

      if (json_reader_read_member (reader, "width")) {
        width = json_reader_get_int_value (reader);
        self->priv->views[i].width = width;
        GST_INFO ("@rentao set view[%d] width=%d", i,
            self->priv->views[i].width);
        json_reader_end_member (reader);
      }

      if (json_reader_read_member (reader, "height")) {
        height = json_reader_get_int_value (reader);
        self->priv->views[i].height = height;
        GST_INFO ("@rentao set view[%d] height=%d", i,
            self->priv->views[i].height);
        json_reader_end_member (reader);
      }

      if (json_reader_read_member (reader, "text")) {
        text = json_reader_get_string_value (reader);
        if (text != NULL) {
          g_strlcpy (self->priv->views[i].text, text, MAX_TEXT_LENGTH);
          GST_INFO ("@rentao set view[%d] text=%s", i,
              self->priv->views[i].text);
        }
        json_reader_end_member (reader);
      }

      if (json_reader_read_member (reader, "x")) {
        x = json_reader_get_int_value (reader);
        self->priv->views[i].x = x;
        GST_INFO ("@rentao set view[%d] left=%d", i, self->priv->views[i].x);
        json_reader_end_member (reader);
      }

      if (json_reader_read_member (reader, "y")) {
        y = json_reader_get_int_value (reader);
        self->priv->views[i].y = y;
        GST_INFO ("@rentao set view[%d] top=%d", i, self->priv->views[i].y);
        json_reader_end_member (reader);
      }

      json_reader_end_element (reader);
      kms_episode_overlay_rebuild_text_images (self);
    }
    GST_INFO ("@rentao set views' count=%d", count);
    json_reader_end_member (reader);
  }

  self->priv->enable = disable;
  KMS_EPISODE_OVERLAY_UNLOCK (self);

  g_object_unref (reader);
  g_object_unref (parser);

  return TRUE;
}

static void
kms_episode_overlay_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  KmsEpisodeOverlay *self = KMS_EPISODE_OVERLAY (object);

  GST_OBJECT_LOCK (self);

  switch (property_id) {
    case PROP_STYLE:
      g_free (self->priv->style);
      self->priv->style = g_value_dup_string (value);
      kms_episode_overlay_parse_style (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (self);
}

static void
kms_episode_overlay_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  gchar style[512];
  KmsEpisodeOverlay *episodeoverlay = KMS_EPISODE_OVERLAY (object);

  GST_OBJECT_LOCK (episodeoverlay);

  switch (property_id) {
    case PROP_STYLE:
      g_snprintf (style, 512, "{}");
      g_value_set_string (value, style);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (episodeoverlay);
}

static void
kms_episode_overlay_initialize_images (KmsEpisodeOverlay * episodeoverlay,
    GstVideoFrame * frame)
{
  if (episodeoverlay->priv->cvImage == NULL) {
    episodeoverlay->priv->cvImage =
        cvCreateImage (cvSize (frame->info.width, frame->info.height),
        IPL_DEPTH_8U, 3);

  } else if ((episodeoverlay->priv->cvImage->width != frame->info.width)
      || (episodeoverlay->priv->cvImage->height != frame->info.height)) {

    cvReleaseImage (&episodeoverlay->priv->cvImage);
    episodeoverlay->priv->cvImage =
        cvCreateImage (cvSize (frame->info.width, frame->info.height),
        IPL_DEPTH_8U, 3);
  }
}

static int
delete_file (const char *fpath, const struct stat *sb, int typeflag,
    struct FTW *ftwbuf)
{
  int rv = g_remove (fpath);

  if (rv) {
    GST_WARNING ("Error deleting file: %s. %s", fpath, strerror (errno));
  }

  return rv;
}

static void
remove_recursive (const gchar * path)
{
  nftw (path, delete_file, 64, FTW_DEPTH | FTW_PHYS);
}

static void
kms_episode_overlay_renderer_image_to_cvimage (cairo_surface_t * text_surface,
    IplImage * curFrameImage, int xpos, int ypos)
{
  int i, j, width, height;
  CvSize frameSize;
  guchar *p, *bitp, *text_image, *pixbuf;

  text_image = cairo_image_surface_get_data (text_surface);
  width = cairo_image_surface_get_width (text_surface);
  height = cairo_image_surface_get_height (text_surface);
  frameSize = cvGetSize (curFrameImage);
  pixbuf = (guchar *) curFrameImage->imageData;

  for (i = 0; i < height && ypos + i < frameSize.height; i++) {
    p = pixbuf + (ypos + i) * frameSize.width * 3 + xpos * 3;
    bitp = text_image + i * width * 4;
    for (j = 0; j < width && j < frameSize.width; j++) {
      p[0] |= bitp[CAIRO_ARGB_B];
      p[1] |= bitp[CAIRO_ARGB_G];
      p[2] |= bitp[CAIRO_ARGB_R];

      bitp += 4;
      p += 3;
    }
  }
}

static GstFlowReturn
kms_episode_overlay_transform_frame_ip (GstVideoFilter * filter,
    GstVideoFrame * frame)
{
  KmsEpisodeOverlay *self = KMS_EPISODE_OVERLAY (filter);
  GstMapInfo info;
  int i;
  IplImage *curImg, *styleZone;

  //CvFont font;
  int left, right, top, bottom;
  KmsTextViewPrivate *data;

  // plug-in now is disabled.
  if (self->priv->enable == 0)
    return GST_FLOW_OK;

//  gst_buffer_map (inbuf, &map, GST_MAP_READ);
//  data = map.data;
//  size = map.size;
//
//  /* somehow pango barfs over "\0" buffers... */
//  while (size > 0 &&
//      (data[size - 1] == '\r' ||
//          data[size - 1] == '\n' || data[size - 1] == '\0')) {
//    size--;
//  }
//
//  /* render text */
//  GST_DEBUG ("rendering '%*s'", (gint) size, data);
//  pango_layout_set_markup (render->layout, (gchar *) data, size);
//  gst_text_render_render_pangocairo (render);
//  gst_buffer_unmap (inbuf, &map);
//
//  gst_text_render_check_argb (render);

  gst_buffer_map (frame->buffer, &info, GST_MAP_READ);

  // Check the current frame's resolution just in case.
  if (frame->info.width != self->priv->output_width
      || frame->info.height != self->priv->output_height) {
    static int id = 0;
    char filename[256];

    // save the frame to file for further checking.
    g_snprintf (filename, 256, "/var/log/kurento-media-server/snapshot%03d.jpg",
        id++);
    GST_ERROR
        ("@rentao wront resolution, output=(%d,%d), current frame=(%d,%d), save this frame to %s.",
        frame->info.width, frame->info.height, self->priv->output_width,
        self->priv->output_height, filename);

    kms_episode_overlay_initialize_images (self, frame);
    self->priv->cvImage->imageData = (char *) info.data;
    curImg = self->priv->cvImage;
    cvSaveImage (filename, curImg, 0);
    gst_buffer_unmap (frame->buffer, &info);
    return GST_FLOW_OK;
  }

  KMS_EPISODE_OVERLAY_LOCK (self);

  kms_episode_overlay_initialize_images (self, frame);
  self->priv->cvImage->imageData = (char *) info.data;
  curImg = self->priv->cvImage;
  GST_TRACE ("@rentao transform_frame_ip. width=%d, height=%d",
      cvGetSize (curImg).width, cvGetSize (curImg).height);

  //cvInitFont (&font, CV_FONT_HERSHEY_SIMPLEX, 0.75f, 0.75f, 0, 2, 8);   //rate of width

  // enable==2 means no view need to show, just show the background image only.
  if (self->priv->enable == 2) {
    KMS_EPISODE_OVERLAY_UNLOCK (self);
    if (self->priv->background_image != NULL) {
      cvResize (self->priv->background_image, curImg, CV_INTER_LINEAR);
    }
    gst_buffer_unmap (frame->buffer, &info);
    return GST_FLOW_OK;
  }
  // try to build the background.
  if (self->priv->background == NULL && self->priv->background_image != NULL) {
    styleZone =
        cvCreateImage (cvGetSize (curImg), curImg->depth, curImg->nChannels);
    cvResize (self->priv->background_image, styleZone, CV_INTER_LINEAR);
//    cvCopy (self->priv->background_image, styleZone, NULL);
    for (i = 0; i < MAX_VIEW_COUNT; i++) {
      data = &self->priv->views[i];
      if (data->width > 0) {
        cvRectangle (styleZone, cvPoint (data->x, data->y),
            cvPoint (data->x + data->width, data->y + data->height), CV_RGB (0,
                0, 0), CV_FILLED, 8, 0);
      }
    }
    self->priv->background = styleZone;
    GST_INFO ("@rentao build background size=%d,%d",
        cvGetSize (styleZone).width, cvGetSize (styleZone).height);
  }
  // draw the background to source frame first.
  if (self->priv->background != NULL) {
//    cvCopy(self->priv->background_image, curImg, NULL);
    cvAdd (curImg, self->priv->background, curImg, NULL);
//    GST_INFO("@rentao add background to source frame");
  }

  for (i = 0; i < MAX_VIEW_COUNT; i++) {
    data = &self->priv->views[i];
    if (data->width > 0) {
      styleZone =
          cvCreateImage (cvSize (data->width, MSG_BAR_HEIGHT), curImg->depth,
          curImg->nChannels);
      // draw background block.
      cvRectangle (styleZone, cvPoint (0, 0), cvPoint (data->width,
              MSG_BAR_HEIGHT), MSG_BAR_BGCOLOR, CV_FILLED, 8, 0);
      // draw leading small color block.
      if (i == 0)
        cvRectangle (styleZone, cvPoint (0, 0), cvPoint (3, MSG_BAR_HEIGHT),
            HOST_BAR_COLOR, CV_FILLED, 8, 0);
      else
        cvRectangle (styleZone, cvPoint (0, 0), cvPoint (3, MSG_BAR_HEIGHT),
            GUEST_BAR_COLOR, CV_FILLED, 8, 0);

      // set ROI
      left = data->x;           // left
      bottom = data->y + data->height;  // bottom
      right = left + data->width;       // right
      top = bottom - MSG_BAR_HEIGHT;    //top
      cvSetImageROI (curImg, cvRect (left, top, right - left, bottom - top));

      // draw style bar alpha transparency to source frame.
      cvAddWeighted (curImg, 1 - MSG_BAR_ALPHA, styleZone, MSG_BAR_ALPHA, 0,
          curImg);

      // draw msg text.
//      cvPutText (curImg, data->text, cvPoint (10, 20), &font, CV_RGB (255, 255,
//              255));
      // release ROI
      cvResetImageROI (curImg);

      // draw msg text.
      if (data->textImage != NULL) {
        int offsetX = 10;       // left-most align.
        int offsetY = (MSG_BAR_HEIGHT - cairo_image_surface_get_height (data->textImage) + 1) / 2;      // center align.

        kms_episode_overlay_renderer_image_to_cvimage (data->textImage, curImg,
            left + offsetX, top + offsetY);
      }
      // draw boundary
      cvRectangle (curImg, cvPoint (data->x, data->y),
          cvPoint (data->x + data->width, data->y + data->height), CV_RGB (255,
              255, 255), 1, 8, 0);
      cvRectangle (curImg, cvPoint (data->x - 1, data->y - 1),
          cvPoint (data->x + data->width + 1, data->y + data->height + 1),
          CV_RGB (255, 255, 255), 1, 8, 0);

      cvReleaseImage (&styleZone);
    }
  }
//
//  // draw text using pango
//  pango_layout_set_markup (self->priv->layout,
//      "Hello!@#World 123aslkdjf;lnxz,.v", 40);
//  if (1) {
//    PangoRectangle ink_rect, logical_rect;
//    gint width, height;
//    cairo_surface_t *surface;
//    cairo_t *cr;
//
//    pango_layout_get_pixel_extents (self->priv->layout, &ink_rect,
//        &logical_rect);
//
//    width = logical_rect.width + self->priv->shadow_offset;
//    height = logical_rect.height + logical_rect.y + self->priv->shadow_offset;
//    GST_INFO ("@rentao width=%d height=%d", width, height);
////    width = 240;
////    height = 80;
//    surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
////    text_image = g_realloc (text_image, 4 * width * height);
////    surface = cairo_image_surface_create_for_data (text_image,
////            CAIRO_FORMAT_ARGB32, width, height, width * 4);
//
//    cr = cairo_create (surface);
//
////    cairo_select_font_face (cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
////    cairo_set_font_size (cr, 12.0);
//    cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
//    cairo_move_to (cr, 0.0, 0.0);
////    cairo_show_text (cr, "?");
//
//    pango_cairo_show_layout (cr, self->priv->layout);
//    cairo_destroy (cr);
//
////    styleZone = cvCreateImage ( cvSize(width, height), 8, 4);
////    styleZone->imageData = (char *)cairo_image_surface_get_data(surface);
////    cvAdd (curImg, styleZone, curImg, NULL);
//
////    cairo_surface_write_to_png (surface,
////        "/var/log/kurento-media-server/hello.png");
////    text_image = cairo_image_surface_get_data (surface);
//    kms_episode_overlay_renderer_image_to_cvimage (surface, curImg, 10, 20);
//    cairo_surface_destroy (surface);
//
////    g_free(text_image);
//  }

  KMS_EPISODE_OVERLAY_UNLOCK (self);

  gst_buffer_unmap (frame->buffer, &info);

  return GST_FLOW_OK;
}

static void
dispose_queue_element (gpointer data)
{
  gst_structure_free (data);
}

static void
kms_episode_overlay_dispose (GObject * object)
{
  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (kms_episode_overlay_parent_class)->dispose (object);
}

static void
kms_episode_overlay_finalize (GObject * object)
{
  int i;

  KmsEpisodeOverlay *episodeoverlay = KMS_EPISODE_OVERLAY (object);

  if (episodeoverlay->priv->cvImage != NULL)
    cvReleaseImage (&episodeoverlay->priv->cvImage);

  if (episodeoverlay->priv->costume != NULL)
    cvReleaseImage (&episodeoverlay->priv->costume);

  if (episodeoverlay->priv->background_image != NULL)
    cvReleaseImage (&episodeoverlay->priv->background_image);

  if (episodeoverlay->priv->background != NULL)
    cvReleaseImage (&episodeoverlay->priv->background);

  if (episodeoverlay->priv->image_to_overlay != NULL)
    gst_structure_free (episodeoverlay->priv->image_to_overlay);

  if (episodeoverlay->priv->dir_created) {
    remove_recursive (episodeoverlay->priv->dir);
    g_free (episodeoverlay->priv->dir);
  }

  if (episodeoverlay->priv->style != NULL)
    g_free (episodeoverlay->priv->style);

  for (i = 0; i < MAX_VIEW_COUNT; i++) {
    if (episodeoverlay->priv->views[i].textImage != NULL)
      cairo_surface_destroy (episodeoverlay->priv->views[i].textImage);
  }

  g_rec_mutex_clear (&episodeoverlay->priv->mutex);

  g_queue_free_full (episodeoverlay->priv->events_queue, dispose_queue_element);
  episodeoverlay->priv->events_queue = NULL;

  G_OBJECT_CLASS (kms_episode_overlay_parent_class)->finalize (object);
}

static void
kms_episode_overlay_init (KmsEpisodeOverlay * self)
{
  int i;
  PangoFontMap *fontmap;
  PangoFontDescription *desc;

  self->priv = KMS_EPISODE_OVERLAY_GET_PRIVATE (self);
  g_rec_mutex_init (&self->priv->mutex);

  self->priv->show_debug_info = FALSE;
  self->priv->cvImage = NULL;
  self->priv->costume = NULL;
  self->priv->background_image = NULL;
  self->priv->background = NULL;
  self->priv->dir_created = FALSE;
  self->priv->style = NULL;
  self->priv->enable = 0;

  self->priv->events_queue = g_queue_new ();

  for (i = 0; i < MAX_VIEW_COUNT; i++) {
    self->priv->views[i].width = -1;
    self->priv->views[i].height = -1;
    self->priv->views[i].textImage = NULL;
  }

  // init pango context
  self->priv->shadow_offset = 0;
  self->priv->outline_offset = 0;
  fontmap = pango_cairo_font_map_get_default ();
  self->priv->pango_context =
      pango_font_map_create_context (PANGO_FONT_MAP (fontmap));
  pango_context_set_base_gravity (self->priv->pango_context,
      PANGO_GRAVITY_SOUTH);
  // init pango layout
  self->priv->layout = pango_layout_new (self->priv->pango_context);
//  desc = pango_context_get_font_description (self->priv->pango_context);
  desc = pango_font_description_from_string ("sans bold 16");
  if (desc != NULL) {
    GST_INFO ("@rentao pango_font_description_from_string");
  }
  pango_layout_set_font_description (self->priv->layout, desc);
//  GST_INFO ("@rentao font description sans bold 64. %" GST_PTR_FORMAT, desc);
  kms_episode_overlay_adjust_values_with_fontdesc (self, desc);

}

static void
kms_episode_overlay_class_init (KmsEpisodeOverlayClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstVideoFilterClass *video_filter_class = GST_VIDEO_FILTER_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, PLUGIN_NAME, 0, PLUGIN_NAME);

  GST_DEBUG ("class init");

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (klass),
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          gst_caps_from_string (VIDEO_SRC_CAPS)));
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (klass),
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          gst_caps_from_string (VIDEO_SINK_CAPS)));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "episode style overlay element", "Video/Filter",
      "Set a style like background, frameset, msg bar over the source frame",
      " Tao Ren <tao@swarmnyc.com> <tour.ren.gz@gmail.com>");

  gobject_class->set_property = kms_episode_overlay_set_property;
  gobject_class->get_property = kms_episode_overlay_get_property;
  gobject_class->dispose = kms_episode_overlay_dispose;
  gobject_class->finalize = kms_episode_overlay_finalize;

  video_filter_class->transform_frame_ip =
      GST_DEBUG_FUNCPTR (kms_episode_overlay_transform_frame_ip);

  /* Properties initialization */
  g_object_class_install_property (gobject_class, PROP_STYLE,
      g_param_spec_string ("style",
          "The showing style at the episode, like name, message, frame",
          "Style description(schema like ice candidate)",
          DEFAULT_STYLE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (klass, sizeof (KmsEpisodeOverlayPrivate));
}

gboolean
kms_episode_overlay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, PLUGIN_NAME, GST_RANK_NONE,
      KMS_TYPE_EPISODE_OVERLAY);
}
