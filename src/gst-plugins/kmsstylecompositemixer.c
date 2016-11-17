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
#define _XOPEN_SOURCE 500

#include "kmsstylecompositemixer.h"
#include <commons/kmsagnosticcaps.h>
#include <commons/kmshubport.h>
#include <commons/kmsloop.h>
#include <commons/kmsrefstruct.h>
#include <math.h>
#include <ftw.h>
#include <stdlib.h>
#include <glib-object.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
#include <glib/gstdio.h>
#include <string.h>

#define LATENCY 600             //ms
#define TEMP_PATH "/tmp/XXXXXX"

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
  int enable;
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
  gint frame_rate;
  gint pad_x, pad_y, line_weight;
  gchar *background_image;
  gchar *style;
  gchar font_desc[64];
  KmsConpositeViewPrivate views[MAX_VIEW_COUNT];
  GstElement *episodeoverlay;
  gboolean dir_created;
  gchar *dir;
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

// resize the source views' resolution to full cover the output resolution and keep the ratio unchanged.
#define SCALE_TO_JUST_FULL_COVER(sw, sh, ow, oh) \
  if ((sw) * (oh) >= (sh) * (ow)) { \
    sw = oh * sw / sh; \
    sh = oh; \
  } else { \
    sh = ow * sh / sw; \
    sw = ow; \
  }

#define CLEANUP_ELEMENT_FROM(self, element) \
    gst_bin_remove (GST_BIN (self), g_object_ref (element)); \
    gst_element_set_state (element, GST_STATE_NULL); \
    g_object_unref (element); \
    element = NULL;

static void
kms_style_composite_mixer_recalculate_sizes (gpointer data)
{
  KmsStyleCompositeMixer *self = KMS_STYLE_COMPOSITE_MIXER (data);

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
  gint i, mappedCount = 0, unmappedCount = 0, curColumn, left, top, occupied;
  KmsStyleCompositeMixerData *viewMapping[MAX_VIEW_COUNT] =
      { NULL, NULL, NULL, NULL };
  KmsStyleCompositeMixerData *viewUnMapping[10] =
      { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };

  if (port_count <= 0) {
    // set to show background only.
    g_object_set (G_OBJECT (self->priv->episodeoverlay), "style",
        "{'enable':2}", NULL);
    return;
  }
  values = g_list_sort (values, compare_port_data);

  // map ports to views.
  mappedCount = 0;
  for (l = values; l != NULL; l = l->next) {
    KmsStyleCompositeMixerData *port_data = l->data;

    if (port_data->input == FALSE) {
      continue;
    }
    // make all the view to transparent first.
    g_object_set (port_data->video_mixer_pad, "xpos", 0, "ypos", 0,
        "alpha", 0.0, NULL);
    if (mappedCount >= MAX_VIEW_COUNT)
      continue;

    // use max-output-bitrate as view id to bind mixer_endpoint to specified user.
    g_object_get (G_OBJECT (port_data->mixer_end_point), "max-output-bitrate",
        &port_data->viewId, NULL);
    // find view that match the ID.
    for (i = 0; i < MAX_VIEW_COUNT; i++) {
      if (self->priv->views[i].id == port_data->viewId) {
        if (self->priv->views[i].enable == 0)
          break;
        if (viewMapping[i] != NULL)
          goto unmapped;
        GST_TRACE ("@rentao mapped id=%d, viewId=%d, i=%d",
            self->priv->views[i].id, port_data->viewId, i);
        viewMapping[i] = port_data;
        mappedCount++;
        break;
      }
    }
    if (i < MAX_VIEW_COUNT)
      continue;                 // break for find a match view.
  unmapped:
    // store the unmapping views.
    for (i = 0; i < 10; i++) {
      if (viewUnMapping[i] == NULL) {
        viewUnMapping[i] = port_data;
        unmappedCount++;
        GST_TRACE ("@rentao unmapped viewId=%d, i=%d, unmappedCount=%d",
            port_data->viewId, i, unmappedCount);
        break;
      }
    }
  }

  // move the unmapped view to the empty slot.
  while (mappedCount < MAX_VIEW_COUNT && unmappedCount > 0) {
    for (i = 0; i < MAX_VIEW_COUNT; i++) {
      if (viewMapping[i] != NULL)
        continue;
      unmappedCount--;
      viewMapping[i] = viewUnMapping[unmappedCount];
      viewUnMapping[unmappedCount] = NULL;
      mappedCount++;
      break;
    }
  }
  n_columns = mappedCount;
  n_rows = 1;
  GST_TRACE_OBJECT (self,
      "@rentao columns=%d rows=%d o_width=%d o_height=%d, mappedCount=%d, unmappedCount=%d, port_count=%d",
      n_columns, n_rows, o_width, o_height, mappedCount, unmappedCount,
      port_count);

  // no view need to show, quit.
  if (n_columns == 0) {
    // set to show background only.
    g_object_set (G_OBJECT (self->priv->episodeoverlay), "style",
        "{'enable':2}", NULL);
    g_list_free (values);
    return;
  }
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
    // current port maybe not match to current view, set it to occupied, that means the port can not use the view's style.
    if (self->priv->views[i].id == port_data->viewId)
      occupied = 0;
    else
      occupied = 1;

    left = pad_left + b_width * curColumn;
    top = pad_top;

    // only one view, show it full screen.
    if (mappedCount <= 1) {
      v_width = o_width - 0;
      v_height = o_height - 0;
      left = 0;
      top = 0;
    }
    g_object_set (port_data->video_mixer_pad, "xpos", left, "ypos", top,
        "width", v_width, "height", v_height, "alpha", 1.0, NULL);

    GST_TRACE_OBJECT (port_data->video_mixer_pad,
        "@rentao top=%d left=%d, pad_left=%d pad_top=%d, v_width=%d v_height=%d",
        top, left, pad_left, pad_top, v_width, v_height);

    // build the view style that will send to the episodeoverlay plugin.
    if (occupied == 0) {
      g_snprintf (jsonView, 512,
          "{'width':%d, 'height':%d, 'x':%d, 'y':%d, 'text':'%s'}%s",
          v_width, v_height, left, top, self->priv->views[i].text,
          (curColumn >= mappedCount - 1) ? "" : ",");
    } else {
      g_snprintf (jsonView, 512,
          "{'width':%d, 'height':%d, 'x':%d, 'y':%d, 'text':'id:%d'}%s",
          v_width, v_height, left, top, port_data->viewId,
          (curColumn >= mappedCount - 1) ? "" : ",");
    }
    g_strlcat (jsonStyle, jsonView, 2048);
    GST_TRACE ("@rentao view json: (%s), i=%d, column=%d", jsonView, i,
        curColumn);
    curColumn++;
  }
  // finishes the view style and set to episodeoverlay plugin.
  g_strlcat (jsonStyle, "]}", 2048);
  GST_TRACE ("@rentao style json: %s", jsonStyle);
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

    gst_element_unlink_many (port_data->capsfilter, port_data->tee,
        port_data->fakesink, NULL);

    CLEANUP_ELEMENT_FROM (self, port_data->capsfilter);
    CLEANUP_ELEMENT_FROM (self, port_data->fakesink);
    CLEANUP_ELEMENT_FROM (self, port_data->tee);
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

//  g_object_set (data->video_mixer_pad, "width", mixer->priv->output_width, "height",
//      mixer->priv->output_height, NULL);

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

  data = kms_create_style_composite_mixer_data ();
  data->mixer = mixer;
  data->id = id;
  data->input = FALSE;
  data->removing = FALSE;
  data->eos_managed = FALSE;

  data->tee = gst_element_factory_make ("tee", NULL);
  data->fakesink = gst_element_factory_make ("fakesink", NULL);
  data->capsfilter = gst_element_factory_make ("capsfilter", NULL);

  g_object_set (G_OBJECT (data->capsfilter), "caps-change-mode",
      1 /*delayed */ , NULL);

  g_object_set (G_OBJECT (data->fakesink), "async", FALSE, "sync", FALSE, NULL);

  gst_bin_add_many (GST_BIN (mixer), data->capsfilter, data->tee,
      data->fakesink, NULL);

  gst_element_sync_state_with_parent (data->capsfilter);
  gst_element_sync_state_with_parent (data->tee);
  gst_element_sync_state_with_parent (data->fakesink);

  /*link basemixer -> video_agnostic */
  kms_base_hub_link_video_sink (KMS_BASE_HUB (mixer), data->id,
      data->capsfilter, "sink", FALSE);

  data->tee_sink_pad = gst_element_get_static_pad (data->tee, "sink");
  gst_element_link_pads (data->capsfilter, NULL, data->tee,
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
//  if (element) GST_TRACE ("@rentao Release request pad %" GST_PTR_FORMAT, element);
//  if (pad) GST_TRACE ("@rentao Release request pad %" GST_PTR_FORMAT, pad);
}

static gboolean
kms_style_composite_mixer_is_valid_uri (const gchar * url)
{
  gboolean ret;
  GRegex *regex;

  regex = g_regex_new ("^(?:((?:https?):)\\/\\/)([^:\\/\\s]+)(?::(\\d*))?(?:\\/"
      "([^\\s?#]+)?([?][^?#]*)?(#.*)?)?$", 0, 0, NULL);
  ret = g_regex_match (regex, url, G_REGEX_MATCH_ANCHORED, NULL);
  g_regex_unref (regex);

  return ret;
}

static gboolean
load_from_url (gchar * file_name, const gchar * url)
{
  SoupSession *session;
  SoupMessage *msg;
  FILE *dst;
  gboolean retOK = FALSE;

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
  retOK = TRUE;

end:
  g_object_unref (msg);
  g_object_unref (session);
  return retOK;
}

static void
kms_style_composite_mixer_setup_background_image (KmsStyleCompositeMixer * self)
{
  gchar *url = self->priv->background_image;
  gchar *file_name;

  // check the videomixer plugin created?
  if (self->priv->videomixer == NULL || url == NULL)
    return;

  // load from url.
  if (kms_style_composite_mixer_is_valid_uri (url)) {
    if (!self->priv->dir_created) {
      gchar *d = g_strdup (TEMP_PATH);

      self->priv->dir = g_mkdtemp (d);
      self->priv->dir_created = TRUE;
      GST_INFO ("@rentao create tmp folder fot download image. %s", d);
    }
    file_name = g_strconcat (self->priv->dir, "/image.png", NULL);

    if (load_from_url (file_name, url)) {
      g_object_set (G_OBJECT (self->priv->videomixer), "background-image",
          file_name, NULL);
      GST_INFO ("@rentao set background file %s ok, and delete it.", file_name);
    }
    g_remove (file_name);
    g_free (file_name);
  }
//
//
//
//  GST_TRACE ("@rentao");
//
//  if (self->priv->episodeoverlay == NULL
//      || self->priv->background_image == NULL)
//    return;
//  g_snprintf (jsonStyle, 512, "{'background_image':'%s'}",
//      self->priv->background_image);
//  g_object_set (G_OBJECT (self->priv->episodeoverlay), "style", jsonStyle,
//      NULL);
//  GST_TRACE ("@rentao style=%s", jsonStyle);
}

static gboolean
kms_style_composite_mixer_parse_style (KmsStyleCompositeMixer * self)
{
  JsonParser *parser;
  GError *error;
  JsonReader *reader;
  gint width = 0, height = 0, frame_rate = 0, pad_x = 0, pad_y = 0, line =
      0, count, i, id, enable, show_hide_flag = 0;
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
  if (width > 0 && self->priv->output_width <= 0) {
    self->priv->output_width = width;
    if (self->priv->pad_x < 0)
      self->priv->pad_x = width / 10;
  }
  json_reader_end_member (reader);

  json_reader_read_member (reader, "height");
  height = json_reader_get_int_value (reader);
  if (height > 0 && self->priv->output_height <= 0) {
    self->priv->output_height = height;
    if (self->priv->pad_y < 0)
      self->priv->pad_y = height / 10;
  }
  json_reader_end_member (reader);

  json_reader_read_member (reader, "frame-rate");
  frame_rate = json_reader_get_int_value (reader);
  if (frame_rate > 0) {
    self->priv->frame_rate = frame_rate;
  }
  json_reader_end_member (reader);

  // handle font description
  if (json_reader_read_member (reader, "font-desc")) {
    fontdesc_str = json_reader_get_string_value (reader);
    if (fontdesc_str != NULL) {
      g_strlcpy (self->priv->font_desc, fontdesc_str, 64);
      GST_TRACE ("@rentao set font-desc=%s", fontdesc_str);
    }
  }
  json_reader_end_element (reader);

  json_reader_read_member (reader, "background");
  background = json_reader_get_string_value (reader);
  if (background != NULL
      && g_strcmp0 (background, self->priv->background_image) != 0) {
    g_free (self->priv->background_image);
    self->priv->background_image = g_strdup (background);
    kms_style_composite_mixer_setup_background_image (self);
    GST_TRACE ("@rentao set background=%s", self->priv->background_image);
  }
  json_reader_end_member (reader);

  json_reader_read_member (reader, "pad-x");
  pad_x = json_reader_get_int_value (reader);
  if (pad_x > 0) {
    self->priv->pad_x = pad_x;
    GST_TRACE ("@rentao set pad_x=%d", pad_x);
  }
  json_reader_end_member (reader);

  json_reader_read_member (reader, "pad-y");
  pad_y = json_reader_get_int_value (reader);
  if (pad_y > 0) {
    self->priv->pad_y = pad_y;
    GST_TRACE ("@rentao set pad_y=%d", pad_y);
  }
  json_reader_end_member (reader);

  json_reader_read_member (reader, "line-weight");
  line = json_reader_get_int_value (reader);
  if (line > 0) {
    self->priv->line_weight = line;
    GST_TRACE ("@rentao set line-weight=%d", line);
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
        GST_TRACE ("@rentao set view[%d] id=%d", i, self->priv->views[i].id);
      } else if (g_strcmp0 (members[mi], "text") == 0) {
        json_reader_read_member (reader, "text");
        text = json_reader_get_string_value (reader);
        if (text != NULL) {
          g_strlcpy (self->priv->views[i].text, text, MAX_TEXT_LENGTH);
          GST_TRACE ("@rentao set view[%d] text=%s", i,
              self->priv->views[i].text);
        }
      } else if (g_strcmp0 (members[mi], "width") == 0) {
        json_reader_read_member (reader, "width");
        width = json_reader_get_int_value (reader);
        if (width > 0) {
          self->priv->views[i].width = width;
          GST_TRACE ("@rentao set view[%d] width=%d", i,
              self->priv->views[i].width);
        }
      } else if (g_strcmp0 (members[mi], "height") == 0) {
        json_reader_read_member (reader, "height");
        height = json_reader_get_int_value (reader);
        if (height > 0) {
          self->priv->views[i].height = height;
          GST_TRACE ("@rentao set view[%d] height=%d", i,
              self->priv->views[i].height);
        }
      } else if (g_strcmp0 (members[mi], "enable") == 0) {
        json_reader_read_member (reader, "enable");
        enable = json_reader_get_int_value (reader);
        if (enable == 0) {
          if (self->priv->views[i].enable != 0)
            show_hide_flag = 1;
          self->priv->views[i].enable = 0;
          GST_TRACE ("@rentao disable view[%d] with id=%d", i,
              self->priv->views[i].id);
        } else {
          if (self->priv->views[i].enable == 0)
            show_hide_flag = 1;
          self->priv->views[i].enable = 1;
        }
      }
      json_reader_end_member (reader);
    }
    g_strfreev (members);
    json_reader_end_element (reader);
  }
  json_reader_end_member (reader);

  GST_TRACE ("@rentao set views' count=%d, show_hide_flag=%d", count,
      show_hide_flag);
  if (show_hide_flag != 0) {
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
        1 /*black */ , "start-time-selection", 1 /*first */ , "width",
        self->priv->output_width, "height", self->priv->output_height, "frame-rate", self->priv->frame_rate, NULL);

    // try to setup background image here, because the background image could be set before this compositor creates.
    kms_style_composite_mixer_setup_background_image (self);

    self->priv->mixer_video_agnostic =
        gst_element_factory_make ("agnosticbin", NULL);

    self->priv->episodeoverlay =
        gst_element_factory_make ("episodeoverlay", NULL);

    gst_bin_add_many (GST_BIN (mixer), self->priv->videomixer,
        self->priv->episodeoverlay, self->priv->mixer_video_agnostic, NULL);

    gst_element_sync_state_with_parent (self->priv->videomixer);
    gst_element_sync_state_with_parent (self->priv->episodeoverlay);
    gst_element_sync_state_with_parent (self->priv->mixer_video_agnostic);

    if (self->priv->videotestsrc == NULL && 1) {
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

      if (self->priv->output_width <= 0) {
        self->priv->output_width = 1280;
        self->priv->output_height = 720;
      }
      filtercaps =
          gst_caps_new_simple ("video/x-raw",
          "width", G_TYPE_INT, 16,
          "height", G_TYPE_INT, 16,
          "framerate", GST_TYPE_FRACTION, self->priv->frame_rate, 1, NULL);
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
      GST_TRACE ("@rentao create videosrc w=%d, h=%d, x=%d, y=%d",
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

//  GST_TRACE ("@rentao, dispose, background=%s", self->priv->background_image);
  G_OBJECT_CLASS (kms_style_composite_mixer_parent_class)->dispose (object);
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

  if (self->priv->dir_created) {
    remove_recursive (self->priv->dir);
    g_free (self->priv->dir);
  }
//  GST_TRACE ("@rentao, finalize, background=%s", self->priv->background_image);
  G_OBJECT_CLASS (kms_style_composite_mixer_parent_class)->finalize (object);
}

static void
kms_style_composite_mixer_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  KmsStyleCompositeMixer *self = KMS_STYLE_COMPOSITE_MIXER (object);

  GST_TRACE ("@rentao kms_composite_get_property %d", property_id);

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

      // change this style format will affect StyleCompositeImpl.cpp function: bool setViewEnableStatus(int viewId, char enable)
      g_snprintf (style, 2048,
          "{'width':%d, 'height':%d, 'frame-rate':%d, 'pad-x':%d, 'pad-y':%d, 'line-weight':%d, 'font-desc':'%s', 'background':'%s', 'views':[{'id':%d, 'enable':%d, 'text':'%s'},{'id':%d, 'enable':%d, 'text':'%s'},{'id':%d, 'enable':%d, 'text':'%s'},{'id':%d, 'enable':%d, 'text':'%s'}]}",
          self->priv->output_width, self->priv->output_height,
          self->priv->frame_rate,
          self->priv->pad_x, self->priv->pad_y, self->priv->line_weight,
          self->priv->font_desc, self->priv->background_image,
          self->priv->views[0].id, self->priv->views[0].enable,
          self->priv->views[0].text, self->priv->views[1].id,
          self->priv->views[1].enable, self->priv->views[1].text,
          self->priv->views[2].id, self->priv->views[2].enable,
          self->priv->views[2].text, self->priv->views[3].id,
          self->priv->views[3].enable, self->priv->views[3].text);
      g_value_set_string (value, style);
      GST_TRACE ("@rentao getStyle(%s)", style);
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

//  GST_TRACE_OBJECT (object, "@rentao kms_composite_set_property %d", prop_id);

  KMS_STYLE_COMPOSITE_MIXER_LOCK (self);
  switch (prop_id) {
    case PROP_BACKGROUND_IMAGE:
      break;
    case PROP_STYLE:
      g_free (self->priv->style);
      self->priv->style = g_value_dup_string (value);
      GST_TRACE ("@rentao setStyle(%s)", self->priv->style);
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
  GST_TRACE ("@rentao Release request pad %s", pad_name);
  GST_TRACE ("@rentao Release request pad %" GST_PTR_FORMAT, self);
  return TRUE;
}

static void
kms_style_composite_mixer_class_init (KmsStyleCompositeMixerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  KmsBaseHubClass *base_hub_class = KMS_BASE_HUB_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  GST_INFO ("@rentao version:6.6.1-debug-frozen");
  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "StyleCompositeMixer", "Generic",
      "Mixer element that composes n input flows in one output flow according to the specified style.",
      "Tao Ren <tao@swarmnyc.com> <tour.ren.gz@gmail.com>");

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
  self->priv->output_height = -1;
  self->priv->output_width = -1;
  self->priv->frame_rate = 15;
  self->priv->pad_x = -1;
  self->priv->pad_y = -1;
  self->priv->line_weight = 2;
  self->priv->n_elems = 0;
  for (i = 0; i < MAX_VIEW_COUNT; i++) {
    self->priv->views[i].id = -1;
    self->priv->views[i].enable = 1;
    self->priv->views[i].width = -1;
    self->priv->views[i].height = -1;
  }
  g_strlcpy (self->priv->font_desc, "sans bold 16", 64);
  self->priv->dir_created = FALSE;
  self->priv->dir = NULL;

  self->priv->loop = kms_loop_new ();
}

gboolean
kms_style_composite_mixer_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, PLUGIN_NAME, GST_RANK_NONE,
      KMS_TYPE_STYLE_COMPOSITE_MIXER);
}
