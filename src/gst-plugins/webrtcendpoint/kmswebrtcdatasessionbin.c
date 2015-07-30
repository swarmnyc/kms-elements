/*
 * (C) Copyright 2015 Kurento (http://kurento.org/)
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
#  include <config.h>
#endif

#include <stdio.h>

#include "kmswebrtcdatasessionbin.h"
#include "kmswebrtcdatachannelbin.h"

#define PLUGIN_NAME "kmswebrtcdatasessionbin"

GST_DEBUG_CATEGORY_STATIC (kms_webrtc_data_session_bin_debug_category);
#define GST_CAT_DEFAULT kms_webrtc_data_session_bin_debug_category

G_DEFINE_TYPE_WITH_CODE (KmsWebRtcDataSessionBin, kms_webrtc_data_session_bin,
    GST_TYPE_BIN,
    GST_DEBUG_CATEGORY_INIT (kms_webrtc_data_session_bin_debug_category,
        PLUGIN_NAME, 0, "debug category for webrtc_data_session_bin"));

#define parent_class kms_webrtc_data_session_bin_parent_class

#define DEFAULT_DTLS_CLIENT_MODE FALSE
#define DEFAULT_SCTP_LOCAL_PORT 0
#define DEFAULT_SCTP_REMOTE_PORT 0

#define SCTP_PORT_MIN 0
#define SCTP_PORT_MAX 65534

#define IS_EVEN(stream_id) (!((stream_id) & 0x01))

#define KMS_WEBRTC_DATA_SESSION_BIN_GET_PRIVATE(obj) ( \
  G_TYPE_INSTANCE_GET_PRIVATE (                        \
    (obj),                                             \
    KMS_TYPE_WEBRTC_DATA_SESSION_BIN,                  \
    KmsWebRtcDataSessionBinPrivate                     \
  )                                                    \
 )

struct _KmsWebRtcDataSessionBinPrivate
{
  guint16 assoc_id;
  gboolean is_client;
  guint16 local_sctp_port;
  guint16 remote_sctp_port;
  GRecMutex mutex;

  GstElement *sctpdec;
  GstElement *sctpenc;

  GHashTable *data_channels;
  guint even_id;
  guint odd_id;
};

#define KMS_WEBRTC_DATA_SESSION_BIN_LOCK(obj) \
  (g_rec_mutex_lock (&KMS_WEBRTC_DATA_SESSION_BIN_CAST ((obj))->priv->mutex))
#define KMS_WEBRTC_DATA_SESSION_BIN_UNLOCK(obj) \
  (g_rec_mutex_unlock (&KMS_WEBRTC_DATA_SESSION_BIN_CAST ((obj))->priv->mutex))

enum
{
  PROP_0,

  PROP_DTLS_CLIENT_MODE,
  PROP_SCTP_LOCAL_PORT,
  PROP_SCTP_REMOTE_PORT,

  N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-sctp"));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-sctp"));

static guint
get_sctp_association_id ()
{
  static guint assoc_id = 0;

  return g_atomic_int_add (&assoc_id, 1);
}

static gchar *
get_decoder_name (guint assoc_id)
{
  return g_strdup_printf ("sctpdec_%u", assoc_id);
}

static gchar *
get_encoder_name (guint assoc_id)
{
  return g_strdup_printf ("sctpenc_%u", assoc_id);
}

static void
kms_webrtc_data_session_bin_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  KmsWebRtcDataSessionBin *self = KMS_WEBRTC_DATA_SESSION_BIN (object);

  KMS_WEBRTC_DATA_SESSION_BIN_LOCK (self);

  switch (property_id) {
    case PROP_DTLS_CLIENT_MODE:
      self->priv->is_client = g_value_get_boolean (value);
      break;
    case PROP_SCTP_LOCAL_PORT:
      self->priv->local_sctp_port = g_value_get_uint (value);
      break;
    case PROP_SCTP_REMOTE_PORT:
      self->priv->remote_sctp_port = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }

  KMS_WEBRTC_DATA_SESSION_BIN_UNLOCK (self);
}

static void
kms_webrtc_data_session_bin_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  KmsWebRtcDataSessionBin *self = KMS_WEBRTC_DATA_SESSION_BIN (object);

  KMS_WEBRTC_DATA_SESSION_BIN_LOCK (self);

  switch (property_id) {
    case PROP_DTLS_CLIENT_MODE:
      g_value_set_boolean (value, self->priv->is_client);
      break;
    case PROP_SCTP_LOCAL_PORT:
      g_value_set_uint (value, self->priv->local_sctp_port);
      break;
    case PROP_SCTP_REMOTE_PORT:
      g_value_set_uint (value, self->priv->remote_sctp_port);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }

  KMS_WEBRTC_DATA_SESSION_BIN_UNLOCK (self);
}

static void
kms_webrtc_data_session_bin_finalize (GObject * object)
{
  KmsWebRtcDataSessionBin *self = KMS_WEBRTC_DATA_SESSION_BIN (object);

  GST_DEBUG_OBJECT (self, "finalize");

  g_rec_mutex_clear (&self->priv->mutex);
  g_hash_table_unref (self->priv->data_channels);

  /* chain up */
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
kms_webrtc_data_session_bin_class_init (KmsWebRtcDataSessionBinClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->set_property = kms_webrtc_data_session_bin_set_property;
  gobject_class->get_property = kms_webrtc_data_session_bin_get_property;
  gobject_class->finalize = kms_webrtc_data_session_bin_finalize;

  gst_element_class_set_details_simple (element_class,
      "SCTP data session management",
      "SCTP/Bin/Connector",
      "Automatically manages WebRTC data establishment protocol",
      "Santiago Carot-Nemesio <sancane_dot_kurento_at_gmail_dot_com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  obj_properties[PROP_DTLS_CLIENT_MODE] =
      g_param_spec_boolean ("dtls-client-mode", "DTLS client mode",
      "Indicates wheter the DTLS role is client or server.",
      DEFAULT_DTLS_CLIENT_MODE,
      G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  obj_properties[PROP_SCTP_LOCAL_PORT] =
      g_param_spec_uint ("sctp-local-port", "SCTP local port",
      "The SCTP port to receive messages", SCTP_PORT_MIN, SCTP_PORT_MAX,
      DEFAULT_SCTP_LOCAL_PORT,
      G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

  obj_properties[PROP_SCTP_REMOTE_PORT] =
      g_param_spec_uint ("sctp-remote-port", "SCTP remote port",
      "The SCTP destination port", SCTP_PORT_MIN, SCTP_PORT_MAX,
      DEFAULT_SCTP_REMOTE_PORT,
      G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

  g_object_class_install_properties (gobject_class, N_PROPERTIES,
      obj_properties);

  g_type_class_add_private (klass, sizeof (KmsWebRtcDataSessionBinPrivate));
}

static gboolean
kms_webrtc_data_session_bin_link_data_channel_sink (KmsWebRtcDataSessionBin *
    self, GstElement * channel, GstPad * srcpad)
{
  gboolean ret = FALSE;
  GstPad *sinkpad;

  sinkpad = gst_element_get_static_pad (channel, "sink");
  ret = gst_pad_link (srcpad, sinkpad) == GST_PAD_LINK_OK;
  g_object_unref (sinkpad);

  return ret;
}

static gboolean
kms_webrtc_data_session_bin_link_data_channel_src (KmsWebRtcDataSessionBin *
    self, GstElement * channel)
{
  GstPadTemplate *pad_template;
  GstPad *srcpad, *sinkpad;
  guint sctp_stream_id;
  gboolean ret = FALSE;
  GstCaps *caps;
  gchar *name;

  g_object_get (G_OBJECT (channel), "id", &sctp_stream_id, NULL);

  caps =
      kms_webrtc_data_channel_bin_create_caps (KMS_WEBRTC_DATA_CHANNEL_BIN
      (channel));

  if (caps == NULL) {
    return FALSE;
  }

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (self->
          priv->sctpenc), "sink_%u");
  name = g_strdup_printf ("sink_%u", sctp_stream_id);
  sinkpad = gst_element_request_pad (self->priv->sctpenc, pad_template, name,
      caps);
  g_free (name);

  srcpad = gst_element_get_static_pad (channel, "src");

  ret = gst_pad_link (srcpad, sinkpad) == GST_PAD_LINK_OK;

  g_object_unref (srcpad);
  g_object_unref (sinkpad);
  gst_caps_unref (caps);

  return ret;
}

static gboolean
kms_webrtc_data_session_bin_is_valid_sctp_stream_id (KmsWebRtcDataSessionBin *
    self, guint16 sctp_stream_id)
{
  if ((sctp_stream_id == 65535) ||
      (IS_EVEN (sctp_stream_id) && self->priv->is_client) ||
      (!IS_EVEN (sctp_stream_id) && !self->priv->is_client)) {
    return FALSE;
  } else {
    return TRUE;
  }
}

static GstElement *
kms_webrtc_data_session_bin_create_remote_data_channel (KmsWebRtcDataSessionBin
    * self, guint sctp_stream_id)
{
  GstElement *channel;

  if (!kms_webrtc_data_session_bin_is_valid_sctp_stream_id (self,
          sctp_stream_id)) {
    GST_WARNING_OBJECT (self, "Invalid data channel requested %u",
        sctp_stream_id);
    return NULL;
  }

  GST_DEBUG_OBJECT (self, "Opened stream id (%u)", sctp_stream_id);

  channel = GST_ELEMENT (kms_webrtc_data_channel_bin_new (sctp_stream_id));
  g_hash_table_insert (self->priv->data_channels,
      GUINT_TO_POINTER (sctp_stream_id), channel);

  return channel;
}

static void
kms_webrtc_data_session_bin_add_remote_data_channel (KmsWebRtcDataSessionBin *
    self, GstElement * channel, GstPad * sctpdec_srcpad)
{
  guint sctp_stream_id;

  g_object_get (channel, "id", &sctp_stream_id, NULL);

  gst_bin_add (GST_BIN (self), channel);

  if (!kms_webrtc_data_session_bin_link_data_channel_src (self, channel) ||
      !kms_webrtc_data_session_bin_link_data_channel_sink (self, channel,
          sctpdec_srcpad)) {
    GST_ERROR_OBJECT (self, "Can not link data channel (%u)", sctp_stream_id);
    return;
  }

  gst_element_sync_state_with_parent (channel);
}

static void
kms_webrtc_data_session_bin_pad_added (GstElement * sctpdec, GstPad * pad,
    KmsWebRtcDataSessionBin * self)
{
  gboolean is_remote = FALSE;
  guint sctp_stream_id;
  GstElement *channel;
  gchar *name;

  name = gst_pad_get_name (pad);
  sscanf (name, "src_%u", &sctp_stream_id);
  g_free (name);

  KMS_WEBRTC_DATA_SESSION_BIN_LOCK (self);

  channel =
      (GstElement *) g_hash_table_lookup (self->priv->data_channels,
      GUINT_TO_POINTER (sctp_stream_id));

  is_remote = channel == NULL;

  if (is_remote) {
    channel = kms_webrtc_data_session_bin_create_remote_data_channel (self,
        sctp_stream_id);
  }

  KMS_WEBRTC_DATA_SESSION_BIN_UNLOCK (self);

  g_return_if_fail (channel != NULL);

  if (is_remote) {
    kms_webrtc_data_session_bin_add_remote_data_channel (self, channel, pad);
    return;
  }

  /* this is a local channel, connect the sink pad to the sctpdec */
  if (!kms_webrtc_data_session_bin_link_data_channel_sink (self, channel, pad)) {
    GST_ERROR_OBJECT (self, "Can not link data channel (%u)", sctp_stream_id);
  }
}

static guint
kms_webrtc_data_session_bin_pick_stream_id (KmsWebRtcDataSessionBin * self)
{
  guint *id;

  if (self->priv->is_client) {
    id = &self->priv->even_id;
  } else {
    id = &self->priv->odd_id;
  }

  while (g_hash_table_contains (self->priv->data_channels,
          GUINT_TO_POINTER (*id))) {
    *id += 2;
  }

  return *id;
}

static void
kms_webrtc_data_session_bin_new_data_channel (KmsWebRtcDataSessionBin * self)
{
  guint sctp_stream_id;
  GstElement *channel;

  KMS_WEBRTC_DATA_SESSION_BIN_LOCK (self);

  sctp_stream_id = kms_webrtc_data_session_bin_pick_stream_id (self);
  channel = GST_ELEMENT (kms_webrtc_data_channel_bin_new (sctp_stream_id));

  gst_bin_add (GST_BIN (self), channel);

  if (!kms_webrtc_data_session_bin_link_data_channel_src (self, channel)) {
    gst_bin_remove (GST_BIN (self), channel);
    GST_ERROR_OBJECT (self, "Can not create data channel for stream id %u",
        sctp_stream_id);
    KMS_WEBRTC_DATA_SESSION_BIN_UNLOCK (self);
  } else {
    g_hash_table_insert (self->priv->data_channels,
        GUINT_TO_POINTER (sctp_stream_id), channel);
    KMS_WEBRTC_DATA_SESSION_BIN_UNLOCK (self);
    gst_element_sync_state_with_parent (channel);
    g_signal_emit_by_name (channel, "request-open", NULL);
  }
}

static void
kms_webrtc_data_session_bin_association_established (GstElement * sctpenc,
    gboolean connected, KmsWebRtcDataSessionBin * self)
{
  KMS_WEBRTC_DATA_SESSION_BIN_LOCK (self);

  if (connected) {
    GST_DEBUG_OBJECT (self, "SCTP association established");
    if (self->priv->is_client) {
      kms_webrtc_data_session_bin_new_data_channel (self);
    }
  } else {
    GST_DEBUG_OBJECT (self, "SCTP association finished");
  }

  KMS_WEBRTC_DATA_SESSION_BIN_UNLOCK (self);
}

static void
kms_webrtc_data_session_bin_init (KmsWebRtcDataSessionBin * self)
{
  GstPadTemplate *pad_template;
  GstPad *pad, *target;
  gchar *name;

  self->priv = KMS_WEBRTC_DATA_SESSION_BIN_GET_PRIVATE (self);

  g_rec_mutex_init (&self->priv->mutex);

  self->priv->data_channels = g_hash_table_new (g_direct_hash, g_direct_equal);
  self->priv->assoc_id = get_sctp_association_id ();
  self->priv->even_id = 0;
  self->priv->odd_id = 1;

  name = get_decoder_name (self->priv->assoc_id);
  self->priv->sctpdec = gst_element_factory_make ("sctpdec", name);
  g_free (name);

  name = get_encoder_name (self->priv->assoc_id);
  self->priv->sctpenc = gst_element_factory_make ("sctpenc", name);
  g_free (name);

  g_object_set (self->priv->sctpdec, "sctp-association-id",
      self->priv->assoc_id, NULL);
  g_object_set (self->priv->sctpenc, "sctp-association-id",
      self->priv->assoc_id, "use-sock-stream", TRUE, NULL);

  g_object_bind_property (self, "sctp-local-port", self->priv->sctpdec,
      "local-sctp-port", G_BINDING_SYNC_CREATE);

  g_object_bind_property (self, "sctp-remote-port", self->priv->sctpenc,
      "remote-sctp-port", G_BINDING_SYNC_CREATE);

  g_signal_connect (self->priv->sctpdec, "pad-added",
      G_CALLBACK (kms_webrtc_data_session_bin_pad_added), self);
  g_signal_connect (self->priv->sctpenc, "sctp-association-established",
      G_CALLBACK (kms_webrtc_data_session_bin_association_established), self);

  gst_bin_add_many (GST_BIN (self), self->priv->sctpdec, self->priv->sctpenc,
      NULL);

  target = gst_element_get_static_pad (self->priv->sctpdec, "sink");
  pad_template = gst_static_pad_template_get (&sink_template);
  pad = gst_ghost_pad_new_from_template ("sink", target, pad_template);
  g_object_unref (pad_template);
  g_object_unref (target);

  gst_element_add_pad (GST_ELEMENT (self), pad);

  target = gst_element_get_static_pad (self->priv->sctpenc, "src");
  pad_template = gst_static_pad_template_get (&src_template);
  pad = gst_ghost_pad_new_from_template ("src", target, pad_template);
  g_object_unref (pad_template);
  g_object_unref (target);

  gst_element_add_pad (GST_ELEMENT (self), pad);

  gst_element_sync_state_with_parent (self->priv->sctpdec);
  gst_element_sync_state_with_parent (self->priv->sctpenc);
}

KmsWebRtcDataSessionBin *
kms_webrtc_data_session_bin_new (gboolean dtls_client_mode)
{
  gpointer *obj;

  obj = g_object_new (KMS_TYPE_WEBRTC_DATA_SESSION_BIN, "dtls-client-mode",
      dtls_client_mode, NULL);

  return KMS_WEBRTC_DATA_SESSION_BIN (obj);
}