/*
 * (C) Copyright 2016 Swarmnyc (http://www.swarmnyc.com/)
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
 * Created by: Tao Ren (tao@swarmnyc.com)
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <glib-object.h>
#include <json-glib/json-glib.h>
#include <gst/gst.h>
#include "kmstextoverlay.h"

#define PLUGIN_NAME "kmstextoverlay"

#define KMS_TEXT_OVERLAY_LOCK(mixer) \
  (g_rec_mutex_lock (&( (KmsTextOverlay *) mixer)->priv->mutex))

#define KMS_TEXT_OVERLAY_UNLOCK(mixer) \
  (g_rec_mutex_unlock (&( (KmsTextOverlay *) mixer)->priv->mutex))

#define DEFAULT_FILTER_TYPE KMS_FILTER_TYPE_AUTODETECT

GST_DEBUG_CATEGORY_STATIC (kms_text_overlay_debug_category);
#define GST_CAT_DEFAULT kms_text_overlay_debug_category

#define KMS_TEXT_OVERLAY_GET_PRIVATE(obj) (\
  G_TYPE_INSTANCE_GET_PRIVATE (            \
    (obj),                                 \
    KMS_TYPE_TEXT_OVERLAY,                 \
    KmsTextOverlayPrivate                  \
  )                                        \
)

#define DEFAULT_STYLE NULL

enum
{
  PROP_0,
  PROP_STYLE,
  N_PROPERTIES
};

struct _KmsTextOverlayPrivate
{
  GRecMutex mutex;
  GstElement *textoverlay;
  gchar *style;
};

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (KmsTextOverlay, kms_text_overlay,
    KMS_TYPE_ELEMENT,
    GST_DEBUG_CATEGORY_INIT (kms_text_overlay_debug_category, PLUGIN_NAME,
        0, "debug category for textoverlay element"));

static gboolean
kms_text_overlay_parse_style (KmsTextOverlay * self)
{
  JsonParser *parser;
  GError *error;
  JsonReader *reader;
  const char *text = "?", *font_desc = "?";
  gint deltay = 0;

  GST_INFO ("@rentao kms_text_overlay_parse_style text=%s, font=%s", text,
      font_desc);
  parser = json_parser_new ();
  error = NULL;
//    json_parser_load_from_data (parser, "{'text':'I am text', 'font-desc':'sans bold 24'}", -1, &error);
  json_parser_load_from_data (parser, self->priv->style, -1, &error);
  if (error) {
    GST_INFO ("@rentao Unable to parse %s, err=%s", self->priv->style,
        error->message);
    g_error_free (error);
    g_object_unref (parser);
    return FALSE;
  }
  reader = json_reader_new (json_parser_get_root (parser));

  json_reader_read_member (reader, "text");
  text = json_reader_get_string_value (reader);
  if (text != NULL) {
    g_object_set (G_OBJECT (self->priv->textoverlay), "text", text, NULL);
  }
  json_reader_end_member (reader);

  json_reader_read_member (reader, "font-desc");
  font_desc = json_reader_get_string_value (reader);
  if (font_desc != NULL) {
    g_object_set (G_OBJECT (self->priv->textoverlay), "font-desc", font_desc,
        NULL);
  }
  json_reader_end_member (reader);

  json_reader_read_member (reader, "deltay");
  deltay = json_reader_get_int_value (reader);
  g_object_set (G_OBJECT (self->priv->textoverlay), "deltay", deltay, NULL);
  json_reader_end_member (reader);

  GST_INFO ("@rentao text=%s, font=%s, deltay=%d", text, font_desc, deltay);
  g_object_unref (reader);
  g_object_unref (parser);

  return TRUE;
}

static void
kms_text_overlay_connect_textoverlay (KmsTextOverlay * self,
    KmsElementPadType type, GstElement * agnosticbin)
{
  GstPad *target, *sink = NULL;
  GstElement *textoverlay, *videoconvert, *decodebin2, *capsfilter, *videorate,
      *videoscale;
  GstCaps *filtercaps;
  gchar *textMsg = "";

  GST_INFO ("@rentao type = %d.", type);
  if (self->priv->style != NULL) {
    textMsg = self->priv->style;
  }
  GST_INFO ("@rentao textMsg = %s.", textMsg);
  if (type == KMS_ELEMENT_PAD_TYPE_VIDEO) {
    // textoverlay source code:
    //   https://code.google.com/p/ossbuild/source/browse/trunk/Main/GStreamer/Source/gst-plugins-base/ext/pango/gsttextoverlay.c?spec=svn1012&r=1007
    textoverlay = gst_element_factory_make ("textoverlay", NULL);
    if (textoverlay == NULL) {
      GST_INFO ("@rentao textoverlay cannot be created.");
      return;
    }
    videoconvert = gst_element_factory_make ("videoconvert", NULL);
    decodebin2 = gst_element_factory_make ("decodebin", NULL);
    capsfilter = gst_element_factory_make ("capsfilter", NULL);
    filtercaps =
        gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING, "AYUV",
//                "width", G_TYPE_INT, 300,
//                "height", G_TYPE_INT, 300,
//        "framerate", GST_TYPE_FRACTION, 15, 1, NULL);
        NULL);
    g_object_set (G_OBJECT (capsfilter), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);

    videorate = gst_element_factory_make ("videorate", NULL);
    videoscale = gst_element_factory_make ("videoscale", NULL);

    g_object_set (G_OBJECT (textoverlay), "text", textMsg, NULL);
    g_object_set (G_OBJECT (textoverlay), "valignment", 1, "halignment", "left",
        "shaded-background", 1, "auto-resize", 0, "font-desc", "sans bold 12",
        "wrap-mode", 0, NULL);
    sink = gst_element_get_static_pad (videoscale, "sink");
    if (sink == NULL) {
      GST_INFO ("@rentao videoconvert sink cannot be created.");
      return;
    }

    GST_INFO ("@rentao linking elements.");
    gst_bin_add_many (GST_BIN (self), videoscale, videoconvert, textoverlay,
        capsfilter, NULL);
    gst_element_link_many (videoscale, videoconvert, capsfilter, textoverlay,
        agnosticbin, NULL);
    gst_element_sync_state_with_parent (textoverlay);
    gst_element_sync_state_with_parent (videoconvert);
    gst_element_sync_state_with_parent (capsfilter);
    gst_element_sync_state_with_parent (decodebin2);
    gst_element_sync_state_with_parent (videoscale);
    gst_element_sync_state_with_parent (videorate);
    self->priv->textoverlay = textoverlay;
  }

  target = gst_element_get_static_pad (agnosticbin, "sink");
  if (target == NULL) {
    GST_INFO ("@rentao agnosticbin sink cannot be created.");
    return;
  }

  GST_INFO ("@rentao sink = %" GST_PTR_FORMAT, sink);
  if (sink == NULL)
    kms_element_connect_sink_target (KMS_ELEMENT (self), target, type);
  else
    kms_element_connect_sink_target (KMS_ELEMENT (self), sink, type);
  g_object_unref (target);
}

static void
kms_text_overlay_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  KmsTextOverlay *self = KMS_TEXT_OVERLAY (object);

  GST_INFO ("@rentao kms_composite_get_property %d", property_id);

  KMS_TEXT_OVERLAY_LOCK (self);

  switch (property_id) {
    case PROP_STYLE:
    {
      g_value_set_string (value, self->priv->style);
      GST_INFO ("@rentao getStyle(%s)", self->priv->style);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }

  KMS_TEXT_OVERLAY_UNLOCK (self);
}

static void
kms_text_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  KmsTextOverlay *self = KMS_TEXT_OVERLAY (object);

  KMS_TEXT_OVERLAY_LOCK (self);
  switch (prop_id) {
    case PROP_STYLE:
      g_free (self->priv->style);
      self->priv->style = g_value_dup_string (value);
//      kms_composite_parse_style (self);
//      if (self->priv->textoverlay != NULL) {
//        g_object_set (G_OBJECT (self->priv->textoverlay), "text",
//            self->priv->style, NULL);
//      }
      GST_INFO ("@rentao setStyle(%s)", self->priv->style);
      kms_text_overlay_parse_style (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  KMS_TEXT_OVERLAY_UNLOCK (self);
}

static void
kms_text_overlay_dispose (GObject * object)
{
  KmsTextOverlay *self = KMS_TEXT_OVERLAY (object);

  g_free (self->priv->style);
  GST_INFO ("@rentao, dispose, style=%s", self->priv->style);
}

static void
kms_text_overlay_finalize (GObject * object)
{
  KmsTextOverlay *self = KMS_TEXT_OVERLAY (object);

  GST_INFO ("@rentao, finalize, style=%s", self->priv->style);
  G_OBJECT_CLASS (kms_text_overlay_parent_class)->finalize (object);
}

static void
kms_text_overlay_class_init (KmsTextOverlayClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "TextOverlay", "Generic/KmsElement", "Kurento text_overlay",
      "Tao Ren <tao@swarmnyc.com, tour.ren.gz@gmail.com>");
  gobject_class->dispose = GST_DEBUG_FUNCPTR (kms_text_overlay_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (kms_text_overlay_finalize);
  gobject_class->set_property = kms_text_overlay_set_property;
  gobject_class->get_property = kms_text_overlay_get_property;

  g_object_class_install_property (gobject_class, PROP_STYLE,
      g_param_spec_string ("style",
          "The showing style at the episode, like name, message, frame",
          "Style description(schema like ice candidate)",
          DEFAULT_STYLE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* Registers a private structure for the instantiatable type */
  g_type_class_add_private (klass, sizeof (KmsTextOverlayPrivate));

}

static void
kms_text_overlay_init (KmsTextOverlay * self)
{
  self->priv = KMS_TEXT_OVERLAY_GET_PRIVATE (self);

  GST_INFO ("@rentao kms_text_overlay started");
  kms_text_overlay_connect_textoverlay (self, KMS_ELEMENT_PAD_TYPE_VIDEO,
      kms_element_get_video_agnosticbin (KMS_ELEMENT (self)));
  kms_text_overlay_connect_textoverlay (self, KMS_ELEMENT_PAD_TYPE_AUDIO,
      kms_element_get_audio_agnosticbin (KMS_ELEMENT (self)));
  GST_INFO ("@rentao kms_text_overlay finished.");
}

gboolean
kms_text_overlay_plugin_init (GstPlugin * plugin)
{

  return gst_element_register (plugin, PLUGIN_NAME, GST_RANK_NONE,
      KMS_TYPE_TEXT_OVERLAY);
}
