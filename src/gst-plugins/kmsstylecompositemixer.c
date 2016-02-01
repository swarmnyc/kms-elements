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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "kmsstylecompositemixer.h"
#include <commons/kmsagnosticcaps.h>
#include <commons/kmshubport.h>
#include <commons/kmsloop.h>
#include <commons/kmsrefstruct.h>
#include <math.h>
#include <stdlib.h>
#include <glib-object.h>
#include <json-glib/json-glib.h>

#define LATENCY 600             //ms

#define PLUGIN_NAME "stylecompositemixer"

#define KMS_STYLE_COMPOSITE_MIXER_LOCK(mixer) \
  (g_rec_mutex_lock (&( (KmsStyleCompositeMixer *) mixer)->priv->mutex))

#define KMS_STYLE_COMPOSITE_MIXER_UNLOCK(mixer) \
  (g_rec_mutex_unlock (&( (KmsStyleCompositeMixer *) mixer)->priv->mutex))

GST_DEBUG_CATEGORY_STATIC (kms_style_composite_mixer_debug_category);
#define GST_CAT_DEFAULT kms_style_composite_mixer_debug_category

#define KMS_STYLE_COMPOSITE_MIXER_GET_PRIVATE(obj) (\
  G_TYPE_INSTANCE_GET_PRIVATE (               \
    (obj),                                    \
    KMS_TYPE_STYLE_COMPOSITE_MIXER,                 \
    KmsStyleCompositeMixerPrivate                  \
  )                                           \
)

#define AUDIO_SINK_PAD_PREFIX_COMP "audio_sink_"
#define VIDEO_SINK_PAD_PREFIX_COMP "video_sink_"
#define AUDIO_SRC_PAD_PREFIX_COMP "audio_src_"
#define VIDEO_SRC_PAD_PREFIX_COMP "video_src_"
#define AUDIO_SINK_PAD_NAME_COMP AUDIO_SINK_PAD_PREFIX_COMP "%u"
#define VIDEO_SINK_PAD_NAME_COMP VIDEO_SINK_PAD_PREFIX_COMP "%u"
#define AUDIO_SRC_PAD_NAME_COMP AUDIO_SRC_PAD_PREFIX_COMP "%u"
#define VIDEO_SRC_PAD_NAME_COMP VIDEO_SRC_PAD_PREFIX_COMP "%u"

#define DEFAULT_BACKGROUND_IMAGE NULL
#define DEFAULT_STYLE NULL

enum
{
  PROP_0,
  PROP_BACKGROUND_IMAGE,
  PROP_STYLE,
  N_PROPERTIES
};

static GstStaticPadTemplate audio_sink_factory =
GST_STATIC_PAD_TEMPLATE (AUDIO_SINK_PAD_NAME_COMP,
    GST_PAD_SINK,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS (KMS_AGNOSTIC_RAW_AUDIO_CAPS)
    );

static GstStaticPadTemplate video_sink_factory =
GST_STATIC_PAD_TEMPLATE (VIDEO_SINK_PAD_NAME_COMP,
    GST_PAD_SINK,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS (KMS_AGNOSTIC_RAW_VIDEO_CAPS)
    );

static GstStaticPadTemplate audio_src_factory =
GST_STATIC_PAD_TEMPLATE (AUDIO_SRC_PAD_NAME_COMP,
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS (KMS_AGNOSTIC_RAW_AUDIO_CAPS)
    );

static GstStaticPadTemplate video_src_factory =
GST_STATIC_PAD_TEMPLATE (VIDEO_SRC_PAD_NAME_COMP,
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS (KMS_AGNOSTIC_RAW_VIDEO_CAPS)
    );

#define MAX_VIEW_COUNT 4
#define MAX_TEXT_LENGTH 128
typedef struct _KmsConpositeViewPrivate
{
  int id;
  int width;
  int height;
  gchar text[MAX_TEXT_LENGTH];
} KmsConpositeViewPrivate;

struct _KmsStyleCompositeMixerPrivate
{
  GstElement *videomixer;
  GstElement *audiomixer;
  GstElement *videotestsrc;
  GHashTable *ports;
  GstElement *mixer_audio_agnostic;
  GstElement *mixer_video_agnostic;
  KmsLoop *loop;
  GRecMutex mutex;
  gint n_elems;
  gint output_width, output_height;
  gint pad_x, pad_y, line_weight;
  gchar *background_image;
  gchar *style;
  gchar font_desc[64];
//  GstElement *source, *jpg_decoder;
//  GstElement *capsfilter, *freeze, *videoconvert, *videorate, *videoscale,
//      *textoverlay;
//  GstCaps *filtercaps;
  KmsConpositeViewPrivate views[MAX_VIEW_COUNT];
  GstElement *episodeoverlay;
};

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (KmsStyleCompositeMixer, kms_style_composite_mixer,
    KMS_TYPE_BASE_HUB,
    GST_DEBUG_CATEGORY_INIT (kms_style_composite_mixer_debug_category,
        PLUGIN_NAME, 0, "debug category for stylecompositemixer element"));

typedef struct _KmsStyleCompositeMixerData
{
  KmsRefStruct parent;
  gint id;
  KmsStyleCompositeMixer *mixer;
  GstElement *capsfilter;
  GstElement *videocrop;
  GstElement *tee;
  GstElement *fakesink;
  gboolean input;
  gboolean removing;
  gboolean eos_managed;
  gulong probe_id;
  gulong link_probe_id;
  gulong latency_probe_id;
  GstPad *video_mixer_pad;
  GstPad *tee_sink_pad;
  GstElement *mixer_end_point;
  gint viewId;
} KmsStyleCompositeMixerData;

#define KMS_STYLE_COMPOSITE_MIXER_REF(data) \
  kms_ref_struct_ref (KMS_REF_STRUCT_CAST (data))
#define KMS_STYLE_COMPOSITE_MIXER_UNREF(data) \
  kms_ref_struct_unref (KMS_REF_STRUCT_CAST (data))

static void
kms_destroy_style_composite_mixer_data (KmsStyleCompositeMixerData * data)
{
  g_slice_free (KmsStyleCompositeMixerData, data);
}

static KmsStyleCompositeMixerData *
kms_create_style_composite_mixer_data ()
{
  KmsStyleCompositeMixerData *data;

  data = g_slice_new0 (KmsStyleCompositeMixerData);
  kms_ref_struct_init (KMS_REF_STRUCT_CAST (data),
      (GDestroyNotify) kms_destroy_style_composite_mixer_data);

  return data;
}

static gint
compare_port_data (gconstpointer a, gconstpointer b)
{
  KmsStyleCompositeMixerData *port_data_a = (KmsStyleCompositeMixerData *) a;
  KmsStyleCompositeMixerData *port_data_b = (KmsStyleCompositeMixerData *) b;

  if (port_data_a->viewId != port_data_b->viewId)
    return port_data_a->viewId - port_data_b->viewId;
  return port_data_a->id - port_data_b->id;
}

#if 0
static gint
get_width_height (GstPad * pad, gint * width, gint * height)
{
  gint ret = 0;
  GstCaps *caps;
  const GstStructure *str;
  gint l_width, l_height;

  if (pad != NULL) {
    caps = gst_pad_get_current_caps (pad);
    if (caps != NULL) {
      str = gst_caps_get_structure (caps, 0);
      if (gst_structure_get_int (str, "width", &l_width) &&
          gst_structure_get_int (str, "height", &l_height)) {
        GST_DEBUG ("@rentao get_width_height width=%d height=%d", l_width,
            l_height);
        *width = l_width;
        *height = l_height;
        ret = 1;
      }
      gst_caps_unref (caps);
    }
  }
  return ret;
}
#endif

static void
kms_style_composite_mixer_recalculate_sizes (gpointer data)
{
  KmsStyleCompositeMixer *self = KMS_STYLE_COMPOSITE_MIXER (data);
  GstCaps *filtercaps;
  gint n_columns, n_rows;
  GList *l;
  GList *values = g_hash_table_get_values (self->priv->ports);
  gint content_width, content_height;
  gchar jsonStyle[2048];
  gchar jsonView[512];

  gint o_width = self->priv->output_width, o_height = self->priv->output_height;
  gint b_width, b_height = 0;
  gint v_width, v_height;
  gint line_weight = self->priv->line_weight;
  gint pad_left, pad_top;
  gint port_count = self->priv->n_elems;
  gint width_reminder;
  gint i, mappedCount = 0, curColumn, left, top, src_width, src_height;
  KmsStyleCompositeMixerData *viewMapping[MAX_VIEW_COUNT] =
      { NULL, NULL, NULL, NULL };

  if (port_count <= 0) {
    return;
  }
  values = g_list_sort (values, compare_port_data);

  // map ports to views.
  mappedCount = 0;
  for (l = values; l != NULL; l = l->next) {
    KmsStyleCompositeMixerData *port_data = l->data;

    // use max-output-bitrate as view id to bind mixer_endpoint to specified user.
    g_object_get (G_OBJECT (port_data->mixer_end_point), "max-output-bitrate",
        &port_data->viewId, NULL);

    if (port_data->input == FALSE) {
      continue;
    }
    // find view that match the ID.
    for (i = 0; i < MAX_VIEW_COUNT; i++) {
      GST_INFO ("@rentao id=%d, viewId=%d, i=%d", self->priv->views[i].id,
          port_data->viewId, i);
      if (self->priv->views[i].id == port_data->viewId) {
        if (viewMapping[i] == NULL) {
          viewMapping[i] = port_data;
          mappedCount++;
          break;
        }
      }
    }
    if (i < MAX_VIEW_COUNT)
      continue;                 // break for find a match view.
    // find view without ID and available.
    for (i = 0; i < MAX_VIEW_COUNT; i++) {
      if (self->priv->views[i].id < 0) {
        if (viewMapping[i] == NULL) {
          viewMapping[i] = port_data;
          mappedCount++;
          break;
        }
      }
    }
    if (mappedCount >= MAX_VIEW_COUNT)
      break;
  }

  n_columns = mappedCount;
  n_rows = 1;

  GST_DEBUG_OBJECT (self,
      "@rentao columns=%d rows=%d o_width=%d o_height=%d, mappedCount=%d, port_count=%d",
      n_columns, n_rows, o_width, o_height, mappedCount, port_count);

  //configure the local stream size, left and top according to master output view.
  content_width = o_width - self->priv->pad_x;
  content_height = o_height - self->priv->pad_y;
  b_width = (content_width - line_weight) / n_columns;
  b_height = content_height + b_height;
  v_width = b_width - line_weight;
  v_height = content_height - line_weight * 2;
  width_reminder = content_width - b_width * n_columns;
  pad_left = (self->priv->pad_x + width_reminder) / 2 + line_weight;
  pad_top = self->priv->pad_y / 2 + line_weight;

  g_snprintf (jsonStyle, 2048,
      "{'width':%d, 'height':%d, 'font-desc':'%s', 'enable':%d, 'views':[",
      o_width, o_height, self->priv->font_desc, (mappedCount <= 1) ? 0 : 1);

  // draw the views.
  curColumn = 0;
  for (i = 0; i < MAX_VIEW_COUNT; i++) {
    KmsStyleCompositeMixerData *port_data = viewMapping[i];

    if (port_data == NULL)
      continue;
    left = pad_left + b_width * curColumn;
    top = pad_top;
    // get the source view's resolution.
    src_width = self->priv->views[i].width;
    src_height = self->priv->views[i].height;
    if (src_width < 0)
      src_width = o_width;
    if (src_height < 0)
      src_height = o_height;

    // only one view, show it full screen.
    if (mappedCount <= 1) {
      src_width = o_width;
      src_height = o_height;
      v_width = o_width;
      v_height = o_height;
      left = 0;
      top = 0;
    }

    filtercaps =
        gst_caps_new_simple ("video/x-raw",
        "width", G_TYPE_INT, src_width, "height", G_TYPE_INT, src_height,
        "framerate", GST_TYPE_FRACTION, 15, 1, "pixel-aspect-ratio",
        GST_TYPE_FRACTION, 1, 1, NULL);
    g_object_set (port_data->capsfilter, "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);

    // crop the source view and paste to master view.
    g_object_set (port_data->videocrop, "top", (src_height - v_height) / 2,
        "bottom", (src_height - v_height) / 2, NULL);
    g_object_set (port_data->videocrop, "left", (src_width - v_width) / 2,
        "right", (src_width - v_width) / 2, NULL);
    g_object_set (port_data->video_mixer_pad, "xpos", left, "ypos", top,
        "alpha", 1.0, NULL);

    GST_DEBUG_OBJECT (self,
        "@rentao top=%d left=%d, pad_left=%d pad_top=%d, src_width=%d src_height=%d, v_width=%d v_height=%d",
        top, left, pad_left, pad_top, src_width, src_height, v_width, v_height);

    // build the view style that will send to the episodeoverlay plugin.
    g_snprintf (jsonView, 512,
        "{'width':%d, 'height':%d, 'x':%d, 'y':%d, 'text':'%s'}%s",
        v_width, v_height, left, top, self->priv->views[i].text,
        (curColumn >= mappedCount - 1) ? "" : ",");
    g_strlcat (jsonStyle, jsonView, 2048);
    GST_INFO ("@rentao view json: (%s), i=%d, column=%d", jsonView, i,
        curColumn);
    curColumn++;
  }
  // finishes the view style and set to episodeoverlay plugin.
  g_strlcat (jsonStyle, "]}", 2048);
  GST_INFO ("@rentao style json: %s", jsonStyle);
  g_object_set (G_OBJECT (self->priv->episodeoverlay), "style", jsonStyle,
      NULL);

  g_list_free (values);
}

static gboolean
remove_elements_from_pipeline (KmsStyleCompositeMixerData * port_data)
{
  KmsStyleCompositeMixer *self = port_data->mixer;

  KMS_STYLE_COMPOSITE_MIXER_LOCK (self);

  gst_element_unlink (port_data->capsfilter, self->priv->videomixer);

  if (port_data->latency_probe_id > 0) {
    gst_pad_remove_probe (port_data->video_mixer_pad,
        port_data->latency_probe_id);
    port_data->latency_probe_id = 0;
  }

  if (port_data->video_mixer_pad != NULL) {
    gst_element_release_request_pad (self->priv->videomixer,
        port_data->video_mixer_pad);
    g_object_unref (port_data->video_mixer_pad);
    port_data->video_mixer_pad = NULL;
  }

  gst_bin_remove_many (GST_BIN (self),
      g_object_ref (port_data->capsfilter),
      g_object_ref (port_data->tee), g_object_ref (port_data->fakesink), NULL);

  kms_base_hub_unlink_video_src (KMS_BASE_HUB (self), port_data->id);

  KMS_STYLE_COMPOSITE_MIXER_UNLOCK (self);

  gst_element_set_state (port_data->capsfilter, GST_STATE_NULL);
  gst_element_set_state (port_data->tee, GST_STATE_NULL);
  gst_element_set_state (port_data->fakesink, GST_STATE_NULL);

  g_object_unref (port_data->capsfilter);
  g_object_unref (port_data->tee);
  g_object_unref (port_data->fakesink);
  g_object_unref (port_data->tee_sink_pad);

  port_data->tee_sink_pad = NULL;
  port_data->capsfilter = NULL;
  port_data->tee = NULL;
  port_data->fakesink = NULL;

  return G_SOURCE_REMOVE;
}

static GstPadProbeReturn
cb_EOS_received (GstPad * pad, GstPadProbeInfo * info, gpointer data)
{
  KmsStyleCompositeMixerData *port_data = (KmsStyleCompositeMixerData *) data;
  KmsStyleCompositeMixer *self = port_data->mixer;
  GstEvent *event;

  if (GST_EVENT_TYPE (GST_PAD_PROBE_INFO_EVENT (info)) != GST_EVENT_EOS) {
    return GST_PAD_PROBE_OK;
  }

  KMS_STYLE_COMPOSITE_MIXER_LOCK (self);

  if (!port_data->removing) {
    port_data->eos_managed = TRUE;
    KMS_STYLE_COMPOSITE_MIXER_UNLOCK (self);
    return GST_PAD_PROBE_OK;
  }

  if (port_data->probe_id > 0) {
    gst_pad_remove_probe (pad, port_data->probe_id);
    port_data->probe_id = 0;
  }

  KMS_STYLE_COMPOSITE_MIXER_UNLOCK (self);

  event = gst_event_new_eos ();
  gst_pad_send_event (pad, event);

  kms_loop_idle_add_full (self->priv->loop, G_PRIORITY_DEFAULT,
      (GSourceFunc) remove_elements_from_pipeline,
      KMS_STYLE_COMPOSITE_MIXER_REF (port_data),
      (GDestroyNotify) kms_ref_struct_unref);

  return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn
cb_latency (GstPad * pad, GstPadProbeInfo * info, gpointer data)
{
  if (GST_QUERY_TYPE (GST_PAD_PROBE_INFO_QUERY (info)) != GST_QUERY_LATENCY) {
    return GST_PAD_PROBE_OK;
  }

  GST_LOG_OBJECT (pad, "Modifing latency query. New latency %ld",
      LATENCY * GST_MSECOND);

  gst_query_set_latency (GST_PAD_PROBE_INFO_QUERY (info),
      TRUE, LATENCY * GST_MSECOND, LATENCY * GST_MSECOND);

  return GST_PAD_PROBE_OK;
}

static void
kms_style_composite_mixer_port_data_destroy (gpointer data)
{
  KmsStyleCompositeMixerData *port_data = (KmsStyleCompositeMixerData *) data;
  KmsStyleCompositeMixer *self = port_data->mixer;
  GstPad *audiosink;
  gchar *padname;

  KMS_STYLE_COMPOSITE_MIXER_LOCK (self);

  port_data->removing = TRUE;

  kms_base_hub_unlink_video_sink (KMS_BASE_HUB (self), port_data->id);
  kms_base_hub_unlink_audio_sink (KMS_BASE_HUB (self), port_data->id);

  if (port_data->input) {
    GstEvent *event;
    gboolean result;
    GstPad *pad;

    if (port_data->capsfilter == NULL) {
      KMS_STYLE_COMPOSITE_MIXER_UNLOCK (self);
      return;
    }

    pad = gst_element_get_static_pad (port_data->capsfilter, "sink");

    if (pad == NULL) {
      KMS_STYLE_COMPOSITE_MIXER_UNLOCK (self);
      return;
    }

    if (!GST_OBJECT_FLAG_IS_SET (pad, GST_PAD_FLAG_EOS)) {

      if (GST_PAD_IS_FLUSHING (pad)) {
        gst_pad_send_event (pad, gst_event_new_flush_stop (FALSE));
      }

      event = gst_event_new_eos ();
      result = gst_pad_send_event (pad, event);

      if (port_data->input && self->priv->n_elems > 0) {
        port_data->input = FALSE;
        self->priv->n_elems--;
        kms_style_composite_mixer_recalculate_sizes (self);
      }
      KMS_STYLE_COMPOSITE_MIXER_UNLOCK (self);

      if (!result) {
        GST_WARNING ("EOS event did not send");
      }
    } else {
      gboolean remove = FALSE;

      /* EOS callback was triggered before we could remove the port data */
      /* so we have to remove elements to avoid memory leaks. */
      remove = port_data->eos_managed;
      KMS_STYLE_COMPOSITE_MIXER_UNLOCK (self);

      if (remove) {
        /* Remove pipeline without helding the mutex */
        kms_loop_idle_add_full (self->priv->loop, G_PRIORITY_DEFAULT,
            (GSourceFunc) remove_elements_from_pipeline,
            KMS_STYLE_COMPOSITE_MIXER_REF (port_data),
            (GDestroyNotify) kms_ref_struct_unref);
      }
    }
    gst_element_unlink (port_data->capsfilter, port_data->tee);
    g_object_unref (pad);
  } else {
    if (port_data->probe_id > 0) {
      gst_pad_remove_probe (port_data->video_mixer_pad, port_data->probe_id);
    }

    if (port_data->latency_probe_id > 0) {
      gst_pad_remove_probe (port_data->video_mixer_pad,
          port_data->latency_probe_id);
    }

    if (port_data->link_probe_id > 0) {
      gst_pad_remove_probe (port_data->tee_sink_pad, port_data->link_probe_id);
    }
    KMS_STYLE_COMPOSITE_MIXER_UNLOCK (self);

    gst_element_unlink (port_data->capsfilter, port_data->tee);
    gst_element_unlink (port_data->tee, port_data->fakesink);

    gst_bin_remove (GST_BIN (self), g_object_ref (port_data->capsfilter));
    gst_element_set_state (port_data->capsfilter, GST_STATE_NULL);
    g_object_unref (port_data->capsfilter);
    port_data->capsfilter = NULL;

    gst_bin_remove (GST_BIN (self), g_object_ref (port_data->tee));
    gst_element_set_state (port_data->tee, GST_STATE_NULL);
    g_object_unref (port_data->tee);
    port_data->tee = NULL;

    gst_bin_remove (GST_BIN (self), g_object_ref (port_data->fakesink));
    gst_element_set_state (port_data->fakesink, GST_STATE_NULL);
    g_object_unref (port_data->fakesink);
    port_data->fakesink = NULL;
  }

  padname = g_strdup_printf (AUDIO_SINK_PAD, port_data->id);
  audiosink = gst_element_get_static_pad (self->priv->audiomixer, padname);
  gst_element_release_request_pad (self->priv->audiomixer, audiosink);
  gst_object_unref (audiosink);
  g_free (padname);
}

static GstPadProbeReturn
link_to_videomixer (GstPad * pad, GstPadProbeInfo * info,
    KmsStyleCompositeMixerData * data)
{
  GstPadTemplate *sink_pad_template;
  KmsStyleCompositeMixer *mixer;
  GstPad *tee_src;

  if (GST_EVENT_TYPE (GST_PAD_PROBE_INFO_EVENT (info)) !=
      GST_EVENT_STREAM_START) {
    return GST_PAD_PROBE_PASS;
  }

  mixer = KMS_STYLE_COMPOSITE_MIXER (data->mixer);
  GST_DEBUG ("stream start detected %d", data->id);
  KMS_STYLE_COMPOSITE_MIXER_LOCK (mixer);

  data->link_probe_id = 0;
  data->latency_probe_id = 0;

  sink_pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (mixer->priv->
          videomixer), "sink_%u");

  if (G_UNLIKELY (sink_pad_template == NULL)) {
    GST_ERROR_OBJECT (mixer, "Error taking a new pad from videomixer");
    KMS_STYLE_COMPOSITE_MIXER_UNLOCK (mixer);
    return GST_PAD_PROBE_DROP;
  }

  data->input = TRUE;

  /*link tee -> videomixer */
  data->video_mixer_pad =
      gst_element_request_pad (mixer->priv->videomixer,
      sink_pad_template, NULL, NULL);

  tee_src = gst_element_get_request_pad (data->tee, "src_%u");

  gst_element_link_pads (data->tee, GST_OBJECT_NAME (tee_src),
      mixer->priv->videomixer, GST_OBJECT_NAME (data->video_mixer_pad));
  g_object_unref (tee_src);

  data->probe_id = gst_pad_add_probe (data->video_mixer_pad,
      GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      (GstPadProbeCallback) cb_EOS_received,
      KMS_STYLE_COMPOSITE_MIXER_REF (data),
      (GDestroyNotify) kms_ref_struct_unref);

  data->latency_probe_id = gst_pad_add_probe (data->video_mixer_pad,
      GST_PAD_PROBE_TYPE_QUERY_UPSTREAM,
      (GstPadProbeCallback) cb_latency, NULL, NULL);

  /*recalculate the output sizes */
  mixer->priv->n_elems++;
  kms_style_composite_mixer_recalculate_sizes (mixer);

  //Recalculate latency to avoid video freezes when an element stops to send media.
  gst_bin_recalculate_latency (GST_BIN (mixer));

  KMS_STYLE_COMPOSITE_MIXER_UNLOCK (mixer);

  return GST_PAD_PROBE_REMOVE;
}

static void
release_gint (gpointer data)
{
  g_slice_free (gint, data);
}

static gint *
create_gint (gint value)
{
  gint *p = g_slice_new (gint);

  *p = value;
  return p;
}

static void
kms_style_composite_mixer_unhandle_port (KmsBaseHub * mixer, gint id)
{
  KmsStyleCompositeMixer *self = KMS_STYLE_COMPOSITE_MIXER (mixer);

  GST_DEBUG ("unhandle id %d", id);

  KMS_STYLE_COMPOSITE_MIXER_LOCK (self);

  g_hash_table_remove (self->priv->ports, &id);

  KMS_STYLE_COMPOSITE_MIXER_UNLOCK (self);

  KMS_BASE_HUB_CLASS (G_OBJECT_CLASS
      (kms_style_composite_mixer_parent_class))->unhandle_port (mixer, id);
}

static KmsStyleCompositeMixerData *
kms_style_composite_mixer_port_data_create (KmsStyleCompositeMixer * mixer,
    gint id)
{
  KmsStyleCompositeMixerData *data;
  gchar *padname;
  GstPad *tee_src;
  GstCaps *filtercaps;

  data = kms_create_style_composite_mixer_data ();
  data->mixer = mixer;
  data->id = id;
  data->input = FALSE;
  data->removing = FALSE;
  data->eos_managed = FALSE;

  data->tee = gst_element_factory_make ("tee", NULL);
  data->fakesink = gst_element_factory_make ("fakesink", NULL);
  data->capsfilter = gst_element_factory_make ("capsfilter", NULL);
  data->videocrop = gst_element_factory_make ("videocrop", NULL);
  g_object_set (data->videocrop, "top", 50, "left", 100, "right", 100, "bottom",
      50, NULL);

  g_object_set (G_OBJECT (data->capsfilter), "caps-change-mode",
      1 /*delayed */ , NULL);

  g_object_set (G_OBJECT (data->fakesink), "async", FALSE, "sync", FALSE, NULL);

  gst_bin_add_many (GST_BIN (mixer), data->capsfilter, data->videocrop,
      data->tee, data->fakesink, NULL);

  gst_element_sync_state_with_parent (data->capsfilter);
  gst_element_sync_state_with_parent (data->tee);
  gst_element_sync_state_with_parent (data->fakesink);
  gst_element_sync_state_with_parent (data->videocrop);

  filtercaps =
      gst_caps_new_simple ("video/x-raw",
      "width", G_TYPE_INT, mixer->priv->output_width,
      "height", G_TYPE_INT, mixer->priv->output_height,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1, NULL);
  g_object_set (data->capsfilter, "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  /*link basemixer -> video_agnostic */
  kms_base_hub_link_video_sink (KMS_BASE_HUB (mixer), data->id,
      data->capsfilter, "sink", FALSE);

  gst_element_link (data->capsfilter, data->videocrop);

  data->tee_sink_pad = gst_element_get_static_pad (data->tee, "sink");
  gst_element_link_pads (data->videocrop, NULL, data->tee,
      GST_OBJECT_NAME (data->tee_sink_pad));

  tee_src = gst_element_get_request_pad (data->tee, "src_%u");
  gst_element_link_pads (data->tee, GST_OBJECT_NAME (tee_src), data->fakesink,
      "sink");
  g_object_unref (tee_src);

  padname = g_strdup_printf (AUDIO_SINK_PAD, id);
  kms_base_hub_link_audio_sink (KMS_BASE_HUB (mixer), id,
      mixer->priv->audiomixer, padname, FALSE);
  g_free (padname);

  data->link_probe_id = gst_pad_add_probe (data->tee_sink_pad,
      GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM | GST_PAD_PROBE_TYPE_BLOCK,
      (GstPadProbeCallback) link_to_videomixer,
      KMS_STYLE_COMPOSITE_MIXER_REF (data),
      (GDestroyNotify) kms_ref_struct_unref);

  return data;
}

static gint
get_stream_id_from_padname (const gchar * name)
{
  gint64 id;

  if (name == NULL)
    return -1;

  if (!g_str_has_prefix (name, AUDIO_SRC_PAD_PREFIX))
    return -1;

  id = g_ascii_strtoll (name + LENGTH_AUDIO_SRC_PAD_PREFIX, NULL, 10);
  if (id > G_MAXINT)
    return -1;

  return id;
}

static void
pad_added_cb (GstElement * element, GstPad * pad, gpointer data)
{
  gint id;
  KmsStyleCompositeMixer *self = KMS_STYLE_COMPOSITE_MIXER (data);

  if (gst_pad_get_direction (pad) != GST_PAD_SRC)
    return;

  id = get_stream_id_from_padname (GST_OBJECT_NAME (pad));

  if (id < 0) {
    GST_ERROR_OBJECT (self, "Invalid HubPort for %" GST_PTR_FORMAT, pad);
    return;
  }

  kms_base_hub_link_audio_src (KMS_BASE_HUB (self), id,
      self->priv->audiomixer, GST_OBJECT_NAME (pad), TRUE);
}

static void
pad_removed_cb (GstElement * element, GstPad * pad, gpointer data)
{
  GST_DEBUG ("Removed pad %" GST_PTR_FORMAT, pad);
}

static void
pad_release_request_cb (GstElement * element, GstPad * pad, gpointer data)
{
  GST_INFO ("@rentao Release request pad %" GST_PTR_FORMAT, element);
  GST_INFO ("@rentao Release request pad %" GST_PTR_FORMAT, pad);
}

static int
create_freezeimage_video (KmsStyleCompositeMixer * self)
{
  GstElement *source, *jpg_decoder;
  GstElement *capsfilter, *freeze, *videoconvert, *videorate, *videoscale,
      *textoverlay;
  GstCaps *filtercaps;
  GstPad *pad;
  GstPadTemplate *sink_pad_template;
  gchar *bg_img;

  if (self->priv->videomixer == NULL)
    return -2;
  bg_img = self->priv->background_image;

  // create the elements.
  if (bg_img == NULL) {
    source = gst_element_factory_make ("souphttpsrc", NULL);
    g_object_set (source, "location", "http://placeimg.com/800/600/any.jpg",
        "is-live", TRUE, NULL);
    GST_INFO ("@rentao using default background image");
  } else if (bg_img[0] == '/') {
    if (!g_file_test (bg_img, G_FILE_TEST_EXISTS)) {
      GST_ERROR ("@rentao file %s not found.", bg_img);
      return -1;
    }
    source = gst_element_factory_make ("filesrc", NULL);
    g_object_set (G_OBJECT (source), "location", bg_img, NULL);
    GST_INFO ("@rentao using local file(%s) as background image", bg_img);
  } else {
    source = gst_element_factory_make ("souphttpsrc", NULL);
    g_object_set (source, "location", bg_img, "is-live", TRUE, NULL);
    GST_INFO ("@rentao using http file(%s) as background image", bg_img);
  }

  filtercaps = gst_caps_new_simple ("image/jpeg",
      "framerate", GST_TYPE_FRACTION, 1, 1, NULL);
  g_object_set (G_OBJECT (source), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  jpg_decoder = gst_element_factory_make ("jpegdec", NULL);
  if (!jpg_decoder) {
    GST_ERROR ("Jpg Decoder could not be created. Exiting.\n");
    return -1;
  }

  sink_pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS
      (self->priv->videomixer), "sink_%u");
  if (G_UNLIKELY (sink_pad_template == NULL)) {
    GST_ERROR_OBJECT (self, "Error taking a new pad from videomixer");
    return -1;
  }

  freeze = gst_element_factory_make ("imagefreeze", NULL);
  videoconvert = gst_element_factory_make ("videoconvert", NULL);
  videorate = gst_element_factory_make ("videorate", NULL);
  videoscale = gst_element_factory_make ("videoscale", NULL);
  // textoverlay source code:
  //   https://code.google.com/p/ossbuild/source/browse/trunk/Main/GStreamer/Source/gst-plugins-base/ext/pango/gsttextoverlay.c?spec=svn1012&r=1007
  textoverlay = gst_element_factory_make ("textoverlay", NULL);
  g_object_set (G_OBJECT (textoverlay),
      "shaded-background", 1, "auto-resize", 0, "font-desc", "sans bold 24",
      "xpad", 54, "ypad", 12, NULL);

  capsfilter = gst_element_factory_make ("capsfilter", NULL);
  filtercaps =
      gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING, "AYUV",
      "width", G_TYPE_INT, self->priv->output_width,
      "height", G_TYPE_INT, self->priv->output_height,
      "framerate", GST_TYPE_FRACTION, 15, 1, NULL);
  g_object_set (G_OBJECT (capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  gst_bin_add_many (GST_BIN (self), source, jpg_decoder, freeze, textoverlay,
      videoconvert, videorate, videoscale, capsfilter, NULL);

  gst_element_link_many (source, jpg_decoder, freeze, textoverlay, videoconvert,
      videorate, videoscale, NULL);
  gst_element_link (videoscale, capsfilter);

  /*link capsfilter -> videomixer */
  pad = gst_element_request_pad (self->priv->videomixer, sink_pad_template,
      NULL, NULL);

  gst_element_link_pads (capsfilter, NULL,
      self->priv->videomixer, GST_OBJECT_NAME (pad));
  g_object_set (pad, "xpos", 0, "ypos", 0, "alpha", 0.9, NULL);
  g_object_unref (pad);

  gst_element_sync_state_with_parent (capsfilter);
  gst_element_sync_state_with_parent (videoscale);
  gst_element_sync_state_with_parent (videorate);
  gst_element_sync_state_with_parent (videoconvert);
  gst_element_sync_state_with_parent (freeze);
  gst_element_sync_state_with_parent (jpg_decoder);
  gst_element_sync_state_with_parent (source);
  gst_element_sync_state_with_parent (textoverlay);
//  self->priv->capsfilter = capsfilter;
//  self->priv->videoscale = videoscale;
//  self->priv->videorate = videorate;
//  self->priv->videoconvert = videoconvert;
//  self->priv->freeze = freeze;
//  self->priv->jpg_decoder = jpg_decoder;
//  self->priv->source = source;
//  self->priv->textoverlay = textoverlay;

  return 0;
}

static void
kms_style_composite_mixer_setup_background_image (KmsStyleCompositeMixer * self)
{
  gchar jsonStyle[512];

  GST_INFO ("@rentao");

  if (self->priv->episodeoverlay == NULL
      || self->priv->background_image == NULL)
    return;
  g_snprintf (jsonStyle, 512, "{'background_image':'%s'}",
      self->priv->background_image);
  g_object_set (G_OBJECT (self->priv->episodeoverlay), "style", jsonStyle,
      NULL);
  GST_INFO ("@rentao style=%s", jsonStyle);
}

static gboolean
kms_style_composite_mixer_parse_style (KmsStyleCompositeMixer * self)
{
  JsonParser *parser;
  GError *error;
  JsonReader *reader;
  gint width = 0, height = 0, pad_x = 0, pad_y = 0, line = 0, count, i, id;
  const gchar *background, *text, *fontdesc_str;
  gchar **members;
  gint n_members, mi;

  parser = json_parser_new ();
  error = NULL;
  json_parser_load_from_data (parser, self->priv->style, -1, &error);
  if (error != NULL) {
    GST_INFO ("@rentao Unable to parse %s, err=%s", self->priv->style,
        error->message);
    g_error_free (error);
    g_object_unref (parser);
    return FALSE;
  }
  reader = json_reader_new (json_parser_get_root (parser));

  KMS_STYLE_COMPOSITE_MIXER_LOCK (self);
  json_reader_read_member (reader, "width");
  width = json_reader_get_int_value (reader);
  if (width > 0) {
    self->priv->output_width = width;
  }
  json_reader_end_member (reader);

  json_reader_read_member (reader, "height");
  height = json_reader_get_int_value (reader);
  if (height > 0) {
    self->priv->output_height = height;
  }
  json_reader_end_member (reader);

  // handle font description
  if (json_reader_read_member (reader, "font-desc")) {
    fontdesc_str = json_reader_get_string_value (reader);
    if (fontdesc_str != NULL) {
      g_strlcpy (self->priv->font_desc, fontdesc_str, 64);
      GST_INFO ("@rentao set font-desc=%s", fontdesc_str);
    }
  }
  json_reader_end_element (reader);

  json_reader_read_member (reader, "background");
  background = json_reader_get_string_value (reader);
  if (background != NULL) {
    g_free (self->priv->background_image);
    self->priv->background_image = g_strdup (background);
    kms_style_composite_mixer_setup_background_image (self);
    GST_INFO ("@rentao set background=%s", self->priv->background_image);
    if (0)
      create_freezeimage_video (self);
  }
  json_reader_end_member (reader);

  json_reader_read_member (reader, "pad-x");
  pad_x = json_reader_get_int_value (reader);
  if (pad_x > 0) {
    self->priv->pad_x = pad_x;
    GST_INFO ("@rentao set pad_x=%d", pad_x);
  }
  json_reader_end_member (reader);

  json_reader_read_member (reader, "pad-y");
  pad_y = json_reader_get_int_value (reader);
  if (pad_y > 0) {
    self->priv->pad_y = pad_y;
    GST_INFO ("@rentao set pad_y=%d", pad_y);
  }
  json_reader_end_member (reader);

  json_reader_read_member (reader, "line-weight");
  line = json_reader_get_int_value (reader);
  if (line > 0) {
    self->priv->line_weight = line;
    GST_INFO ("@rentao set line-weight=%d", line);
  }
  json_reader_end_member (reader);

  json_reader_read_member (reader, "views");
  count = json_reader_count_elements (reader);
  count = (count < MAX_VIEW_COUNT) ? count : MAX_VIEW_COUNT;
  for (i = 0; i < count; i++) {
    json_reader_read_element (reader, i);

    members = json_reader_list_members (reader);
    n_members = g_strv_length (members);

    for (mi = 0; mi < n_members; mi++) {
      if (g_strcmp0 (members[mi], "id") == 0) {
        json_reader_read_member (reader, "id");
        id = json_reader_get_int_value (reader);
        self->priv->views[i].id = id;
        GST_INFO ("@rentao set view[%d] id=%d", i, self->priv->views[i].id);
      } else if (g_strcmp0 (members[mi], "text") == 0) {
        json_reader_read_member (reader, "text");
        text = json_reader_get_string_value (reader);
        if (text != NULL) {
          g_strlcpy (self->priv->views[i].text, text, MAX_TEXT_LENGTH);
          GST_INFO ("@rentao set view[%d] text=%s", i,
              self->priv->views[i].text);
        }
      } else if (g_strcmp0 (members[mi], "width") == 0) {
        json_reader_read_member (reader, "width");
        width = json_reader_get_int_value (reader);
        if (width > 0) {
          self->priv->views[i].width = width;
          GST_INFO ("@rentao set view[%d] width=%d", i,
              self->priv->views[i].width);
        }
      } else if (g_strcmp0 (members[mi], "height") == 0) {
        json_reader_read_member (reader, "height");
        height = json_reader_get_int_value (reader);
        if (height > 0) {
          self->priv->views[i].height = height;
          GST_INFO ("@rentao set view[%d] height=%d", i,
              self->priv->views[i].height);
        }
      }
      json_reader_end_member (reader);
    }
    g_strfreev (members);
    json_reader_end_element (reader);
  }
  json_reader_end_member (reader);

  GST_INFO ("@rentao set views' count=%d", count);
  if (count > 0) {
    kms_style_composite_mixer_recalculate_sizes (self);
  }
  KMS_STYLE_COMPOSITE_MIXER_UNLOCK (self);

  g_object_unref (reader);
  g_object_unref (parser);

  return TRUE;
}

static gint
kms_style_composite_mixer_handle_port (KmsBaseHub * mixer,
    GstElement * mixer_end_point)
{
  KmsStyleCompositeMixer *self = KMS_STYLE_COMPOSITE_MIXER (mixer);
  KmsStyleCompositeMixerData *port_data;
  gint port_id;

  port_id = KMS_BASE_HUB_CLASS (G_OBJECT_CLASS
      (kms_style_composite_mixer_parent_class))->handle_port (mixer,
      mixer_end_point);
  GST_DEBUG ("handle new port, id=%d", port_id);

  if (port_id < 0) {
    return port_id;
  }

  KMS_STYLE_COMPOSITE_MIXER_LOCK (self);

  if (self->priv->videomixer == NULL) {
    self->priv->videomixer = gst_element_factory_make ("compositor", NULL);
    g_object_set (G_OBJECT (self->priv->videomixer), "background",
        1 /*black */ , "start-time-selection", 1 /*first */ , NULL);
    self->priv->mixer_video_agnostic =
        gst_element_factory_make ("agnosticbin", NULL);

    self->priv->episodeoverlay =
        gst_element_factory_make ("episodeoverlay", NULL);
    kms_style_composite_mixer_setup_background_image (self);

    gst_bin_add_many (GST_BIN (mixer), self->priv->videomixer,
        self->priv->episodeoverlay, self->priv->mixer_video_agnostic, NULL);

    gst_element_sync_state_with_parent (self->priv->videomixer);
    gst_element_sync_state_with_parent (self->priv->episodeoverlay);
    gst_element_sync_state_with_parent (self->priv->mixer_video_agnostic);

    if (self->priv->videotestsrc == NULL) {
      GstElement *capsfilter;
      GstCaps *filtercaps;
      GstPad *pad;
      GstPadTemplate *sink_pad_template;
      int pad_x = self->priv->pad_x, pad_y = self->priv->pad_y;

      sink_pad_template =
          gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS
          (self->priv->videomixer), "sink_%u");

      if (G_UNLIKELY (sink_pad_template == NULL)) {
        GST_ERROR_OBJECT (self, "Error taking a new pad from videomixer");
      }

      self->priv->videotestsrc =
          gst_element_factory_make ("videotestsrc", NULL);
      g_object_set (self->priv->videotestsrc, "is-live", TRUE, "pattern",
          /* black */ 2, NULL);

      capsfilter = gst_element_factory_make ("capsfilter", NULL);
      g_object_set (G_OBJECT (capsfilter), "caps-change-mode", 1, NULL);

      filtercaps =
          gst_caps_new_simple ("video/x-raw",
          "width", G_TYPE_INT, self->priv->output_width,
          "height", G_TYPE_INT, self->priv->output_height,
          "framerate", GST_TYPE_FRACTION, 15, 1, NULL);
      g_object_set (G_OBJECT (capsfilter), "caps", filtercaps, NULL);
      gst_caps_unref (filtercaps);

      gst_bin_add_many (GST_BIN (self), self->priv->videotestsrc,
          capsfilter, NULL);
      gst_element_link_many (self->priv->videotestsrc, capsfilter, NULL);

      /*link capsfilter -> videomixer */
      pad = gst_element_request_pad (self->priv->videomixer, sink_pad_template,
          NULL, NULL);
      gst_element_link_pads (capsfilter, NULL,
          self->priv->videomixer, GST_OBJECT_NAME (pad));
      g_object_set (pad, "xpos", 0, "ypos", 0, "alpha", 1.0, NULL);
//      g_object_set (pad, "xpos", pad_x / 2, "ypos", pad_y / 2, "alpha", .6,
//              NULL);
      g_object_unref (pad);
      GST_ERROR ("@rentao create videosrc w=%d, h=%d, x=%d, y=%d",
          self->priv->output_width, self->priv->output_height, pad_x, pad_y);

      gst_element_sync_state_with_parent (capsfilter);
      gst_element_sync_state_with_parent (self->priv->videotestsrc);
    }

    gst_element_link_many (self->priv->videomixer, self->priv->episodeoverlay,
        self->priv->mixer_video_agnostic, NULL);
  }

  if (self->priv->audiomixer == NULL) {
    self->priv->audiomixer = gst_element_factory_make ("kmsaudiomixer", NULL);

    gst_bin_add (GST_BIN (mixer), self->priv->audiomixer);

    gst_element_sync_state_with_parent (self->priv->audiomixer);
    g_signal_connect (self->priv->audiomixer, "pad-added",
        G_CALLBACK (pad_added_cb), self);
    g_signal_connect (self->priv->audiomixer, "pad-removed",
        G_CALLBACK (pad_removed_cb), self);
  }
  kms_base_hub_link_video_src (KMS_BASE_HUB (self), port_id,
      self->priv->mixer_video_agnostic, "src_%u", TRUE);

  port_data = kms_style_composite_mixer_port_data_create (self, port_id);
  port_data->mixer_end_point = mixer_end_point;
  g_signal_connect (port_data->mixer_end_point, "release-requested-pad",
      G_CALLBACK (pad_release_request_cb), self);

  g_hash_table_insert (self->priv->ports, create_gint (port_id), port_data);

  KMS_STYLE_COMPOSITE_MIXER_UNLOCK (self);

  GST_DEBUG ("Finish handle new port, id=%d", port_id);
  return port_id;
}

static void
kms_style_composite_mixer_dispose (GObject * object)
{
  KmsStyleCompositeMixer *self = KMS_STYLE_COMPOSITE_MIXER (object);

  KMS_STYLE_COMPOSITE_MIXER_LOCK (self);
  g_hash_table_remove_all (self->priv->ports);
  KMS_STYLE_COMPOSITE_MIXER_UNLOCK (self);
  g_clear_object (&self->priv->loop);

//  GST_INFO ("@rentao, dispose, background=%s", self->priv->background_image);
  G_OBJECT_CLASS (kms_style_composite_mixer_parent_class)->dispose (object);
}

static void
kms_style_composite_mixer_finalize (GObject * object)
{
  KmsStyleCompositeMixer *self = KMS_STYLE_COMPOSITE_MIXER (object);

  g_rec_mutex_clear (&self->priv->mutex);

  if (self->priv->ports != NULL) {
    g_hash_table_unref (self->priv->ports);
    self->priv->ports = NULL;
  }

  if (self->priv->background_image != NULL)
    g_free (self->priv->background_image);

  if (self->priv->style != NULL)
    g_free (self->priv->style);

//  GST_INFO ("@rentao, finalize, background=%s", self->priv->background_image);
  G_OBJECT_CLASS (kms_style_composite_mixer_parent_class)->finalize (object);
}

static void
kms_style_composite_mixer_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  KmsStyleCompositeMixer *self = KMS_STYLE_COMPOSITE_MIXER (object);

  GST_ERROR ("@rentao kms_composite_get_property %d", property_id);

  KMS_STYLE_COMPOSITE_MIXER_LOCK (self);

  switch (property_id) {
    case PROP_BACKGROUND_IMAGE:
    {
      g_value_set_string (value, self->priv->background_image);
      break;
    }
    case PROP_STYLE:
    {
      gchar style[2048];

      g_snprintf (style, 2048,
          "{width:%d, height:%d, 'pad-x':%d, 'pad-y':%d, 'line-weight':%d, 'font-desc':'%s', background:'%s', views:[{id=%d, text='%s'},{id=%d, text='%s'},{id=%d, text='%s'},{id=%d, text='%s'}]}",
          self->priv->output_width, self->priv->output_height,
          self->priv->pad_x, self->priv->pad_y, self->priv->line_weight,
          self->priv->font_desc, self->priv->background_image,
          self->priv->views[0].id, self->priv->views[0].text,
          self->priv->views[1].id, self->priv->views[1].text,
          self->priv->views[2].id, self->priv->views[2].text,
          self->priv->views[3].id, self->priv->views[3].text);
      g_value_set_string (value, style);
      GST_INFO ("@rentao getStyle(%s)", style);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }

  KMS_STYLE_COMPOSITE_MIXER_UNLOCK (self);
}

static void
kms_style_composite_mixer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  KmsStyleCompositeMixer *self = KMS_STYLE_COMPOSITE_MIXER (object);

//  GST_ERROR_OBJECT (object, "@rentao kms_composite_set_property %d", prop_id);

  KMS_STYLE_COMPOSITE_MIXER_LOCK (self);
  switch (prop_id) {
    case PROP_BACKGROUND_IMAGE:
//      g_free (self->priv->background_image);
//      self->priv->background_image = g_value_dup_string (value);
//      ret = create_freezeimage_video (self);
//
//      GST_ERROR_OBJECT (object, "@rentao create_freezeimage_video return %d",
//          ret);
      break;
    case PROP_STYLE:
      g_free (self->priv->style);
      self->priv->style = g_value_dup_string (value);
      GST_INFO ("@rentao setStyle(%s)", self->priv->style);
      kms_style_composite_mixer_parse_style (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  KMS_STYLE_COMPOSITE_MIXER_UNLOCK (self);
}

static gboolean
kms_style_composite_mixer_release_requested_pad_action (KmsElement * self,
    const gchar * pad_name)
{
  GST_INFO ("@rentao Release request pad %s", pad_name);
  GST_INFO ("@rentao Release request pad %" GST_PTR_FORMAT, self);
  return TRUE;
}

static void
kms_style_composite_mixer_class_init (KmsStyleCompositeMixerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  KmsBaseHubClass *base_hub_class = KMS_BASE_HUB_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  GST_ERROR ("@rentao");
  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "StyleCompositeMixer", "Generic",
      "Mixer element that composes n input flows" " in one output flow",
      "David Fernandez <d.fernandezlop@gmail.com>");

  gobject_class->dispose =
      GST_DEBUG_FUNCPTR (kms_style_composite_mixer_dispose);
  gobject_class->finalize =
      GST_DEBUG_FUNCPTR (kms_style_composite_mixer_finalize);
  gobject_class->set_property = kms_style_composite_mixer_set_property;
  gobject_class->get_property = kms_style_composite_mixer_get_property;

  g_signal_new ("release-requested-pad",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (KmsStyleCompositeMixerClass, release_requested_pad),
      NULL, NULL, NULL, G_TYPE_BOOLEAN, 1, G_TYPE_STRING);
  klass->release_requested_pad =
      GST_DEBUG_FUNCPTR
      (kms_style_composite_mixer_release_requested_pad_action);

  base_hub_class->handle_port =
      GST_DEBUG_FUNCPTR (kms_style_composite_mixer_handle_port);
  base_hub_class->unhandle_port =
      GST_DEBUG_FUNCPTR (kms_style_composite_mixer_unhandle_port);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&audio_src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&video_src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&audio_sink_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&video_sink_factory));

  g_object_class_install_property (gobject_class, PROP_BACKGROUND_IMAGE,
      g_param_spec_string ("background-image",
          "Background Image show at the episode",
          "Background Image URI(http file or local file)",
          DEFAULT_BACKGROUND_IMAGE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_STYLE,
      g_param_spec_string ("style",
          "The showing style at the episode, like name, message, frame",
          "Style description(schema like ice candidate)",
          DEFAULT_STYLE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* Registers a private structure for the instantiatable type */
  g_type_class_add_private (klass, sizeof (KmsStyleCompositeMixerPrivate));
}

static void
kms_style_composite_mixer_init (KmsStyleCompositeMixer * self)
{
  int i;

  self->priv = KMS_STYLE_COMPOSITE_MIXER_GET_PRIVATE (self);

  g_rec_mutex_init (&self->priv->mutex);

  self->priv->ports = g_hash_table_new_full (g_int_hash, g_int_equal,
      release_gint, kms_style_composite_mixer_port_data_destroy);
  self->priv->videomixer = NULL;
  self->priv->audiomixer = NULL;
  self->priv->videotestsrc = NULL;
  self->priv->mixer_audio_agnostic = NULL;
  self->priv->mixer_video_agnostic = NULL;
  self->priv->background_image = NULL;
  self->priv->style = NULL;
  //TODO:Obtain the dimensions of the bigger input stream
  self->priv->output_height = 720;
  self->priv->output_width = 1280;
  self->priv->pad_x = self->priv->output_width / 10;
  self->priv->pad_y = self->priv->output_height / 10;
  self->priv->line_weight = 2;
  self->priv->n_elems = 0;
  for (i = 0; i < MAX_VIEW_COUNT; i++) {
    self->priv->views[i].id = -1;
    self->priv->views[i].width = -1;
    self->priv->views[i].height = -1;
  }
  g_strlcpy (self->priv->font_desc, "sans bold 16", 64);

  self->priv->loop = kms_loop_new ();
}

gboolean
kms_style_composite_mixer_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, PLUGIN_NAME, GST_RANK_NONE,
      KMS_TYPE_STYLE_COMPOSITE_MIXER);
}
