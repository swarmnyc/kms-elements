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

#define TEMP_PATH "/tmp/XXXXXX"
#define BLUE_COLOR (cvScalar (255, 0, 0, 0))
#define SRC_OVERLAY ((double)1)

#define PLUGIN_NAME "episodeoverlay"

GST_DEBUG_CATEGORY_STATIC (kms_episode_overlay_debug_category);
#define GST_CAT_DEFAULT kms_episode_overlay_debug_category

#define KMS_EPISODE_OVERLAY_GET_PRIVATE(obj) ( \
  G_TYPE_INSTANCE_GET_PRIVATE (              \
    (obj),                                   \
    KMS_TYPE_EPISODE_OVERLAY,                  \
    KmsEpisodeOverlayPrivate                   \
  )                                          \
)

enum
{
  PROP_0,
  PROP_IMAGE_TO_OVERLAY,
  PROP_SHOW_DEBUG_INFO
};

struct _KmsEpisodeOverlayPrivate
{
  IplImage *cvImage, *costume;
  GstStructure *image_to_overlay;

  gdouble offsetXPercent, offsetYPercent, widthPercent, heightPercent;
  gboolean show_debug_info, dir_created;
  gchar *dir;
  GstClockTime dts, pts;

  GQueue *events_queue;
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
load_from_url (gchar * file_name, gchar * url)
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
kms_episode_overlay_load_image_to_overlay (KmsEpisodeOverlay * episodeoverlay)
{
  gchar *url = NULL;
  IplImage *costumeAux = NULL;
  gboolean fields_ok = TRUE;

  fields_ok = fields_ok
      && gst_structure_get (episodeoverlay->priv->image_to_overlay,
      "offsetXPercent", G_TYPE_DOUBLE, &episodeoverlay->priv->offsetXPercent,
      NULL);
  fields_ok = fields_ok
      && gst_structure_get (episodeoverlay->priv->image_to_overlay,
      "offsetYPercent", G_TYPE_DOUBLE, &episodeoverlay->priv->offsetYPercent,
      NULL);
  fields_ok = fields_ok
      && gst_structure_get (episodeoverlay->priv->image_to_overlay,
      "widthPercent", G_TYPE_DOUBLE, &episodeoverlay->priv->widthPercent, NULL);
  fields_ok = fields_ok
      && gst_structure_get (episodeoverlay->priv->image_to_overlay,
      "heightPercent", G_TYPE_DOUBLE, &episodeoverlay->priv->heightPercent,
      NULL);
  fields_ok = fields_ok
      && gst_structure_get (episodeoverlay->priv->image_to_overlay, "url",
      G_TYPE_STRING, &url, NULL);

  if (!fields_ok) {
    GST_WARNING_OBJECT (episodeoverlay, "Invalid image structure received");
    goto end;
  }

  if (url == NULL) {
    GST_DEBUG ("Unset the image overlay");
    goto end;
  }

  if (!episodeoverlay->priv->dir_created) {
    gchar *d = g_strdup (TEMP_PATH);

    episodeoverlay->priv->dir = g_mkdtemp (d);
    episodeoverlay->priv->dir_created = TRUE;
  }

  costumeAux = cvLoadImage (url, CV_LOAD_IMAGE_UNCHANGED);

  if (costumeAux != NULL) {
    GST_DEBUG ("Image loaded from file");
    goto end;
  }

  if (kms_episode_overlay_is_valid_uri (url)) {
    gchar *file_name =
        g_strconcat (episodeoverlay->priv->dir, "/image.png", NULL);
    load_from_url (file_name, url);
    costumeAux = cvLoadImage (file_name, CV_LOAD_IMAGE_UNCHANGED);
    g_remove (file_name);
    g_free (file_name);
  }

  if (costumeAux == NULL) {
    GST_DEBUG ("Image not loaded");
  } else {
    GST_DEBUG ("Image loaded from URL");
  }

end:

  if (episodeoverlay->priv->costume != NULL) {
    cvReleaseImage (&episodeoverlay->priv->costume);
    episodeoverlay->priv->costume = NULL;
  }

  if (costumeAux != NULL) {
    episodeoverlay->priv->costume = costumeAux;
  }

  g_free (url);
}

static void
kms_episode_overlay_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  KmsEpisodeOverlay *episodeoverlay = KMS_EPISODE_OVERLAY (object);

  GST_OBJECT_LOCK (episodeoverlay);

  switch (property_id) {
    case PROP_SHOW_DEBUG_INFO:
      episodeoverlay->priv->show_debug_info = g_value_get_boolean (value);
      break;
    case PROP_IMAGE_TO_OVERLAY:
      if (episodeoverlay->priv->image_to_overlay != NULL)
        gst_structure_free (episodeoverlay->priv->image_to_overlay);

      episodeoverlay->priv->image_to_overlay = g_value_dup_boxed (value);
      kms_episode_overlay_load_image_to_overlay (episodeoverlay);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (episodeoverlay);
}

static void
kms_episode_overlay_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  KmsEpisodeOverlay *episodeoverlay = KMS_EPISODE_OVERLAY (object);

  GST_DEBUG_OBJECT (episodeoverlay, "get_property");

  GST_OBJECT_LOCK (episodeoverlay);

  switch (property_id) {
    case PROP_SHOW_DEBUG_INFO:
      g_value_set_boolean (value, episodeoverlay->priv->show_debug_info);
      break;
    case PROP_IMAGE_TO_OVERLAY:
      if (episodeoverlay->priv->image_to_overlay == NULL) {
        episodeoverlay->priv->image_to_overlay =
            gst_structure_new_empty ("image_to_overlay");
      }
      g_value_set_boxed (value, episodeoverlay->priv->image_to_overlay);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (episodeoverlay);
}

static void
kms_episode_overlay_display_detections_overlay_img (KmsEpisodeOverlay *
    episodeoverlay, const GSList * faces_list)
{
  const GSList *iterator = NULL;

  for (iterator = faces_list; iterator; iterator = iterator->next) {
    CvRect *r = iterator->data;
    IplImage *costumeAux;
    int w, h;
    uchar *row, *image_row;

    if ((episodeoverlay->priv->heightPercent == 0) ||
        (episodeoverlay->priv->widthPercent == 0)) {
      continue;
    }

    r->x = r->x + (r->width * (episodeoverlay->priv->offsetXPercent));
    r->y = r->y + (r->height * (episodeoverlay->priv->offsetYPercent));
    r->height = r->height * (episodeoverlay->priv->heightPercent);
    r->width = r->width * (episodeoverlay->priv->widthPercent);

    costumeAux = cvCreateImage (cvSize (r->width, r->height),
        episodeoverlay->priv->costume->depth,
        episodeoverlay->priv->costume->nChannels);
    cvResize (episodeoverlay->priv->costume, costumeAux, CV_INTER_LINEAR);

    row = (uchar *) costumeAux->imageData;
    image_row = (uchar *) episodeoverlay->priv->cvImage->imageData +
        (r->y * episodeoverlay->priv->cvImage->widthStep);

    for (h = 0; h < costumeAux->height; h++) {

      uchar *column = row;
      uchar *image_column = image_row + (r->x * 3);

      for (w = 0; w < costumeAux->width; w++) {
        /* Check if point is inside overlay boundaries */
        if (((w + r->x) < episodeoverlay->priv->cvImage->width)
            && ((w + r->x) >= 0)) {
          if (((h + r->y) < episodeoverlay->priv->cvImage->height)
              && ((h + r->y) >= 0)) {

            if (episodeoverlay->priv->costume->nChannels == 1) {
              *(image_column) = (uchar) (*(column));
              *(image_column + 1) = (uchar) (*(column));
              *(image_column + 2) = (uchar) (*(column));
            } else if (episodeoverlay->priv->costume->nChannels == 3) {
              *(image_column) = (uchar) (*(column));
              *(image_column + 1) = (uchar) (*(column + 1));
              *(image_column + 2) = (uchar) (*(column + 2));
            } else if (episodeoverlay->priv->costume->nChannels == 4) {
              double proportion =
                  ((double) *(uchar *) (column + 3)) / (double) 255;
              double overlay = SRC_OVERLAY * proportion;
              double original = 1 - overlay;

              *image_column =
                  (uchar) ((*column * overlay) + (*image_column * original));
              *(image_column + 1) =
                  (uchar) ((*(column + 1) * overlay) + (*(image_column +
                          1) * original));
              *(image_column + 2) =
                  (uchar) ((*(column + 2) * overlay) + (*(image_column +
                          2) * original));
            }
          }
        }

        column += episodeoverlay->priv->costume->nChannels;
        image_column += episodeoverlay->priv->cvImage->nChannels;
      }

      row += costumeAux->widthStep;
      image_row += episodeoverlay->priv->cvImage->widthStep;
    }

    cvReleaseImage (&costumeAux);
  }
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

static GSList *
get_faces (GstStructure * faces)
{
  gint len, aux;
  GSList *list = NULL;

  len = gst_structure_n_fields (faces);

  for (aux = 0; aux < len; aux++) {
    GstStructure *face;
    gboolean ret;

    const gchar *name = gst_structure_nth_field_name (faces, aux);

    if (g_strcmp0 (name, "timestamp") == 0) {
      continue;
    }

    ret = gst_structure_get (faces, name, GST_TYPE_STRUCTURE, &face, NULL);

    if (ret) {
      CvRect *aux = g_slice_new0 (CvRect);

      gst_structure_get (face, "x", G_TYPE_UINT, &aux->x, NULL);
      gst_structure_get (face, "y", G_TYPE_UINT, &aux->y, NULL);
      gst_structure_get (face, "width", G_TYPE_UINT, &aux->width, NULL);
      gst_structure_get (face, "height", G_TYPE_UINT, &aux->height, NULL);
      gst_structure_free (face);
      list = g_slist_append (list, aux);
    }
  }
  return list;
}

static void
kms_episode_overlay_get_timestamp (KmsEpisodeOverlay * episodeoverlay,
    GstStructure * faces)
{
  GstStructure *timestamp;
  gboolean ret;

  ret =
      gst_structure_get (faces, "timestamp", GST_TYPE_STRUCTURE, &timestamp,
      NULL);
  if (ret) {
    gst_structure_get (timestamp, "dts", G_TYPE_UINT64,
        &episodeoverlay->priv->dts, NULL);
    gst_structure_get (timestamp, "pts", G_TYPE_UINT64,
        &episodeoverlay->priv->pts, NULL);
    gst_structure_free (timestamp);
  }

}

static void
cvrect_free (gpointer data)
{
  g_slice_free (CvRect, data);
}

static GstFlowReturn
kms_episode_overlay_transform_frame_ip (GstVideoFilter * filter,
    GstVideoFrame * frame)
{
  KmsEpisodeOverlay *episodeoverlay = KMS_EPISODE_OVERLAY (filter);
  GstMapInfo info;
  GstStructure *faces;
  GSList *faces_list;

  gst_buffer_map (frame->buffer, &info, GST_MAP_READ);

  kms_episode_overlay_initialize_images (episodeoverlay, frame);
  episodeoverlay->priv->cvImage->imageData = (char *) info.data;

  GST_OBJECT_LOCK (episodeoverlay);
  faces = g_queue_pop_head (episodeoverlay->priv->events_queue);

  while (faces != NULL) {

    kms_episode_overlay_get_timestamp (episodeoverlay, faces);
    GST_DEBUG ("buffer pts %" G_GUINT64_FORMAT, frame->buffer->pts);
    GST_DEBUG ("event pts %" G_GUINT64_FORMAT, episodeoverlay->priv->pts);
    GST_DEBUG ("queue length %d",
        g_queue_get_length (episodeoverlay->priv->events_queue));

    if (episodeoverlay->priv->pts == frame->buffer->pts) {
      faces_list = get_faces (faces);

      if (faces_list != NULL) {
        if (episodeoverlay->priv->costume != NULL) {
          kms_episode_overlay_display_detections_overlay_img (episodeoverlay,
              faces_list);
        }
        g_slist_free_full (faces_list, cvrect_free);
      }
      gst_structure_free (faces);
      break;
    } else if (episodeoverlay->priv->pts < frame->buffer->pts) {
      gst_structure_free (faces);
    } else {
      g_queue_push_head (episodeoverlay->priv->events_queue, faces);
      break;
    }
    faces = g_queue_pop_head (episodeoverlay->priv->events_queue);
  }

  GST_OBJECT_UNLOCK (episodeoverlay);

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
  KmsEpisodeOverlay *episodeoverlay = KMS_EPISODE_OVERLAY (object);

  if (episodeoverlay->priv->cvImage != NULL)
    cvReleaseImage (&episodeoverlay->priv->cvImage);

  if (episodeoverlay->priv->costume != NULL)
    cvReleaseImage (&episodeoverlay->priv->costume);

  if (episodeoverlay->priv->image_to_overlay != NULL)
    gst_structure_free (episodeoverlay->priv->image_to_overlay);

  if (episodeoverlay->priv->dir_created) {
    remove_recursive (episodeoverlay->priv->dir);
    g_free (episodeoverlay->priv->dir);
  }

  g_queue_free_full (episodeoverlay->priv->events_queue, dispose_queue_element);
  episodeoverlay->priv->events_queue = NULL;

  G_OBJECT_CLASS (kms_episode_overlay_parent_class)->finalize (object);
}

static void
kms_episode_overlay_init (KmsEpisodeOverlay * episodeoverlay)
{
  GST_ERROR ("@rentao");
  episodeoverlay->priv = KMS_EPISODE_OVERLAY_GET_PRIVATE (episodeoverlay);

  episodeoverlay->priv->show_debug_info = FALSE;
  episodeoverlay->priv->cvImage = NULL;
  episodeoverlay->priv->costume = NULL;
  episodeoverlay->priv->dir_created = FALSE;

  episodeoverlay->priv->events_queue = g_queue_new ();
}

static gboolean
kms_episode_overlay_sink_events (GstBaseTransform * trans, GstEvent * event)
{
  KmsEpisodeOverlay *episodeoverlay = KMS_EPISODE_OVERLAY (trans);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      GstStructure *faces;

      GST_OBJECT_LOCK (episodeoverlay);

      faces = gst_structure_copy (gst_event_get_structure (event));
      g_queue_push_tail (episodeoverlay->priv->events_queue, faces);

      GST_OBJECT_UNLOCK (episodeoverlay);
      break;
    }
    default:
      break;
  }

  return gst_pad_event_default (trans->sinkpad, GST_OBJECT (trans), event);
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
      "image overlay element", "Video/Filter",
      "Set a defined image in a defined position",
      "David Fernandez <d.fernandezlop@gmail.com>");

  gobject_class->set_property = kms_episode_overlay_set_property;
  gobject_class->get_property = kms_episode_overlay_get_property;
  gobject_class->dispose = kms_episode_overlay_dispose;
  gobject_class->finalize = kms_episode_overlay_finalize;

  video_filter_class->transform_frame_ip =
      GST_DEBUG_FUNCPTR (kms_episode_overlay_transform_frame_ip);

  /* Properties initialization */
  g_object_class_install_property (gobject_class, PROP_SHOW_DEBUG_INFO,
      g_param_spec_boolean ("show-debug-region", "show debug region",
          "show evaluation regions over the image", FALSE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_IMAGE_TO_OVERLAY,
      g_param_spec_boxed ("image-to-overlay", "image to overlay",
          "set the url of the image to overlay in the image",
          GST_TYPE_STRUCTURE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  klass->base_facedetector_class.parent_class.sink_event =
      GST_DEBUG_FUNCPTR (kms_episode_overlay_sink_events);

  g_type_class_add_private (klass, sizeof (KmsEpisodeOverlayPrivate));
}

gboolean
kms_episode_overlay_plugin_init (GstPlugin * plugin)
{
  GST_ERROR ("@rentao");
  return gst_element_register (plugin, PLUGIN_NAME, GST_RANK_NONE,
      KMS_TYPE_EPISODE_OVERLAY);
}
