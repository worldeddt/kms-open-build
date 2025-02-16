/*
 * (C) Copyright 2015 Kurento (http://kurento.org/)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "kmssrtpconnection.h"

#define GST_CAT_DEFAULT kmsrtpconnection
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define GST_DEFAULT_NAME "kmsrtpconnection"
#define kms_srtp_connection_parent_class parent_class

#define KMS_SRTP_CONNECTION_GET_PRIVATE(obj) (  \
  G_TYPE_INSTANCE_GET_PRIVATE (                 \
    (obj),                                      \
    KMS_TYPE_SRTP_CONNECTION,                   \
    KmsSrtpConnectionPrivate                    \
  )                                             \
)

enum
{
  PROP_0,
  PROP_ADDED,
  PROP_CONNECTED,
  PROP_IS_CLIENT,
  PROP_MIN_PORT,
  PROP_MAX_PORT
};

enum
{
  /* signals */
  SIGNAL_KEY_SOFT_LIMIT,

  LAST_SIGNAL
};

static guint obj_signals[LAST_SIGNAL] = { 0 };

struct _KmsSrtpConnectionPrivate
{
  gboolean added;
  gboolean connected;
  gboolean is_client;
};

static void
kms_srtp_connection_interface_init (KmsIRtpConnectionInterface * iface);

G_DEFINE_TYPE_WITH_PRIVATE (KmsSrtpConnection, kms_srtp_connection,
    KMS_TYPE_RTP_BASE_CONNECTION);

//G_DEFINE_TYPE_WITH_CODE (KmsSrtpConnection, kms_srtp_connection,
//    KMS_TYPE_RTP_BASE_CONNECTION,
//    G_IMPLEMENT_INTERFACE (KMS_TYPE_I_RTP_CONNECTION,
//        kms_srtp_connection_interface_init));

static gchar *auths[] = {
  NULL,
  "hmac-sha1-32",
  "hmac-sha1-80"
};

static gchar *ciphers[] = {
  NULL,
  "aes-128-icm",
  "aes-256-icm"
};

static guint
kms_srtp_connection_get_rtp_port (KmsRtpBaseConnection * base_conn)
{
  KmsSrtpConnection *self = KMS_SRTP_CONNECTION (base_conn);

  return kms_socket_get_port (self->rtp_socket);
}

static guint
kms_srtp_connection_get_rtcp_port (KmsRtpBaseConnection * base_conn)
{
  KmsSrtpConnection *self = KMS_SRTP_CONNECTION (base_conn);

  return kms_socket_get_port (self->rtcp_socket);
}

static void
kms_srtp_connection_set_remote_info (KmsRtpBaseConnection * base_conn,
    const gchar * host, gint rtp_port, gint rtcp_port)
{
  KmsSrtpConnection *self = KMS_SRTP_CONNECTION (base_conn);

  GST_INFO_OBJECT (self, "Set remote host: %s, RTP: %d, RTCP: %d",
      host, rtp_port, rtcp_port);

  g_signal_emit_by_name (self->rtp_udpsink, "add", host, rtp_port, NULL);
  g_signal_emit_by_name (self->rtcp_udpsink, "add", host, rtcp_port, NULL);
}

static void
kms_srtp_connection_add (KmsIRtpConnection * base_rtp_conn, GstBin * bin,
    gboolean active)
{
  KmsSrtpConnection *self = KMS_SRTP_CONNECTION (base_rtp_conn);

  self->priv->is_client = active;

  gst_bin_add_many (bin, g_object_ref (self->rtp_udpsink),
      g_object_ref (self->rtp_udpsrc),
      g_object_ref (self->rtcp_udpsink),
      g_object_ref (self->rtcp_udpsrc),
      g_object_ref (self->srtpenc), g_object_ref (self->srtpdec), NULL);

  gst_element_link_pads (self->rtp_udpsrc, "src", self->srtpdec, "rtp_sink");
  gst_element_link_pads (self->rtcp_udpsrc, "src", self->srtpdec, "rtcp_sink");
}

static void
kms_srtp_connection_src_sync_state_with_parent (KmsIRtpConnection *
    base_rtp_conn)
{
  KmsSrtpConnection *self = KMS_SRTP_CONNECTION (base_rtp_conn);

  gst_element_sync_state_with_parent (self->srtpdec);
  gst_element_sync_state_with_parent (self->rtp_udpsrc);
  gst_element_sync_state_with_parent (self->rtcp_udpsrc);
}

static void
kms_srtp_connection_sink_sync_state_with_parent (KmsIRtpConnection *
    base_rtp_conn)
{
  KmsSrtpConnection *self = KMS_SRTP_CONNECTION (base_rtp_conn);

  gst_element_sync_state_with_parent (self->srtpenc);
  gst_element_sync_state_with_parent (self->rtp_udpsink);
  gst_element_sync_state_with_parent (self->rtcp_udpsink);
}

static GstPad *
kms_srtp_connection_request_rtp_sink (KmsIRtpConnection * base_rtp_conn)
{
  KmsSrtpConnection *self = KMS_SRTP_CONNECTION (base_rtp_conn);

  return gst_element_request_pad_simple (self->srtpenc, "rtp_sink_0");
}

static GstPad *
kms_srtp_connection_request_rtp_src (KmsIRtpConnection * base_rtp_conn)
{
  KmsSrtpConnection *self = KMS_SRTP_CONNECTION (base_rtp_conn);

  return gst_element_get_static_pad (self->srtpdec, "rtp_src");
}

static GstPad *
kms_srtp_connection_request_rtcp_sink (KmsIRtpConnection * base_rtp_conn)
{
  KmsSrtpConnection *self = KMS_SRTP_CONNECTION (base_rtp_conn);

  return gst_element_request_pad_simple (self->srtpenc, "rtcp_sink_0");
}

static GstPad *
kms_srtp_connection_request_rtcp_src (KmsIRtpConnection * base_rtp_conn)
{
  KmsSrtpConnection *self = KMS_SRTP_CONNECTION (base_rtp_conn);

  return gst_element_get_static_pad (self->srtpdec, "rtcp_src");
}

static void
kms_srtp_connection_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  KmsSrtpConnection *self = KMS_SRTP_CONNECTION (object);

  switch (prop_id) {
    case PROP_ADDED:
      self->priv->added = g_value_get_boolean (value);
      break;
    case PROP_CONNECTED:
      self->priv->connected = g_value_get_boolean (value);
      break;
    case PROP_MIN_PORT:
      self->parent.min_port = g_value_get_uint (value);
      break;
    case PROP_MAX_PORT:
      self->parent.max_port = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
kms_srtp_connection_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  KmsSrtpConnection *self = KMS_SRTP_CONNECTION (object);

  switch (prop_id) {
    case PROP_ADDED:
      g_value_set_boolean (value, self->priv->added);
      break;
    case PROP_CONNECTED:
      g_value_set_boolean (value, self->priv->connected);
      break;
    case PROP_IS_CLIENT:
      g_value_set_boolean (value, self->priv->is_client);
      break;
    case PROP_MIN_PORT:
      g_value_set_uint (value, self->parent.min_port);
      break;
    case PROP_MAX_PORT:
      g_value_set_uint (value, self->parent.max_port);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
kms_srtp_connection_new_pad_cb (GstElement * element, GstPad * pad,
    KmsSrtpConnection * conn)
{
  GstPadTemplate *templ;
  GstPad *sinkpad = NULL;

  templ = gst_pad_get_pad_template (pad);

  if (g_strcmp0 (GST_PAD_TEMPLATE_NAME_TEMPLATE (templ), "rtp_src_%u") == 0) {
    sinkpad = gst_element_get_static_pad (conn->rtp_udpsink, "sink");
  } else if (g_strcmp0 (GST_PAD_TEMPLATE_NAME_TEMPLATE (templ),
          "rtcp_src_%u") == 0) {
    sinkpad = gst_element_get_static_pad (conn->rtcp_udpsink, "sink");
  } else {
    goto end;
  }

  gst_pad_link (pad, sinkpad);

end:
  g_object_unref (templ);
  g_clear_object (&sinkpad);
}

static const gchar *
get_str_auth (guint auth)
{
  const gchar *str_auth = NULL;

  if (auth < G_N_ELEMENTS (auths)) {
    str_auth = auths[auth];
  }

  return str_auth;
}

static const gchar *
get_str_cipher (guint cipher)
{
  const gchar *str_cipher = NULL;

  if (cipher < G_N_ELEMENTS (ciphers)) {
    str_cipher = ciphers[cipher];
  }

  return str_cipher;
}

static GstCaps *
create_key_caps (guint ssrc, const gchar * key, guint auth, guint cipher)
{
  const gchar *str_cipher = NULL, *str_auth = NULL;
  GstBuffer *buff_key;
  guint8 *bin_buff;
  GstCaps *caps;
  gsize len;

  str_cipher = get_str_cipher (cipher);
  str_auth = get_str_auth (auth);

  if (str_cipher == NULL || str_auth == NULL) {
    return NULL;
  }

  bin_buff = g_base64_decode (key, &len);
  buff_key = gst_buffer_new_wrapped (bin_buff, len);

  caps = gst_caps_new_simple ("application/x-srtp",
      "srtp-key", GST_TYPE_BUFFER, buff_key,
      "srtp-cipher", G_TYPE_STRING, str_cipher,
      "srtp-auth", G_TYPE_STRING, str_auth,
      "srtcp-cipher", G_TYPE_STRING, str_cipher,
      "srtcp-auth", G_TYPE_STRING, str_auth, NULL);

  gst_buffer_unref (buff_key);

  return caps;
}

static GstCaps *
kms_srtp_connection_request_remote_key_cb (GstElement * srtpdec, guint ssrc,
    KmsSrtpConnection * conn)
{
  GstCaps *caps = NULL;

  KMS_RTP_BASE_CONNECTION_LOCK (conn);

  if (!conn->r_key_set) {
    GST_DEBUG_OBJECT (conn, "key is not yet set");
    goto end;
  }

  if (!conn->r_updated) {
    GST_DEBUG_OBJECT (conn, "Key is not yet updated");
  } else {
    GST_DEBUG_OBJECT (conn, "Using new key");
    conn->r_updated = FALSE;
  }

  caps = create_key_caps (ssrc, conn->r_key, conn->r_auth,
      conn->r_cipher);

  GST_DEBUG_OBJECT (srtpdec, "Key Caps: %" GST_PTR_FORMAT, caps);

end:
  KMS_RTP_BASE_CONNECTION_UNLOCK (conn);

  return caps;
}

static GstCaps *
kms_srtp_connection_soft_key_limit_cb (GstElement * srtpdec, guint ssrc,
    KmsSrtpConnection * conn)
{
  g_signal_emit (conn, obj_signals[SIGNAL_KEY_SOFT_LIMIT], 0);

  /* FIXME: Key is about to expire, a new one should be provided */
  /* when renegotiation is supported */

  return NULL;
}

KmsSrtpConnection *
kms_srtp_connection_new (guint16 min_port, guint16 max_port, gboolean use_ipv6)
{
  GObject *obj;
  KmsSrtpConnection *conn;
  GSocketFamily socket_family;

  obj = g_object_new (KMS_TYPE_SRTP_CONNECTION, NULL);
  conn = KMS_SRTP_CONNECTION (obj);

  if (use_ipv6) {
    socket_family = G_SOCKET_FAMILY_IPV6;
  } else {
    socket_family = G_SOCKET_FAMILY_IPV4;
  }

  if (!kms_rtp_connection_get_rtp_rtcp_sockets
      (&conn->rtp_socket, &conn->rtcp_socket, min_port, max_port,
          socket_family)) {
    GST_ERROR_OBJECT (obj, "Cannot get ports");
    g_object_unref (obj);
    return NULL;
  }

  conn->r_updated = FALSE;
  conn->r_key_set = FALSE;

  conn->srtpenc = gst_element_factory_make ("srtpenc", NULL);
  conn->srtpdec = gst_element_factory_make ("srtpdec", NULL);
  g_signal_connect (conn->srtpenc, "pad-added",
      G_CALLBACK (kms_srtp_connection_new_pad_cb), obj);
  g_signal_connect (conn->srtpdec, "request-key",
      G_CALLBACK (kms_srtp_connection_request_remote_key_cb), obj);
  g_signal_connect (conn->srtpdec, "soft-limit",
      G_CALLBACK (kms_srtp_connection_soft_key_limit_cb), obj);

  conn->rtp_udpsink = gst_element_factory_make ("multiudpsink", NULL);
  conn->rtp_udpsrc = gst_element_factory_make ("udpsrc", NULL);
  g_object_set (conn->rtp_udpsink, "socket", conn->rtp_socket,
      "sync", FALSE, "async", FALSE, NULL);
  g_object_set (conn->rtp_udpsrc, "socket", conn->rtp_socket, "auto-multicast",
      FALSE, NULL);

  conn->rtcp_udpsink = gst_element_factory_make ("multiudpsink", NULL);
  conn->rtcp_udpsrc = gst_element_factory_make ("udpsrc", NULL);
  g_object_set (conn->rtcp_udpsink, "socket", conn->rtcp_socket,
      "sync", FALSE, "async", FALSE, NULL);
  g_object_set (conn->rtcp_udpsrc, "socket", conn->rtcp_socket,
      "auto-multicast", FALSE, NULL);

  kms_i_rtp_connection_connected_signal (KMS_I_RTP_CONNECTION (conn));

  return conn;
}

static void
kms_srtp_connection_enable_latency_stats (KmsRtpBaseConnection * base)
{
  KmsSrtpConnection *self = KMS_SRTP_CONNECTION (base);
  GstPad *pad;

  kms_rtp_base_connection_remove_probe (base, self->rtp_udpsrc, "src",
      base->src_probe);
  pad = gst_element_get_static_pad (self->rtp_udpsrc, "src");
  base->src_probe = kms_stats_add_buffer_latency_meta_probe (pad, FALSE,
      0 /* No matter type at this point */ );
  g_object_unref (pad);

  kms_rtp_base_connection_remove_probe (base, self->rtp_udpsink, "sink",
      base->sink_probe);
  pad = gst_element_get_static_pad (self->rtp_udpsink, "sink");
  base->sink_probe = kms_stats_add_buffer_latency_notification_probe (pad,
      base->cb, TRUE /* Lock the data */ , base->user_data, NULL);
  g_object_unref (pad);
}

void
kms_srtp_transport_disable_latency_notification (KmsRtpBaseConnection * base)
{
  KmsSrtpConnection *self = KMS_SRTP_CONNECTION (base);

  kms_rtp_base_connection_remove_probe (base, self->rtp_udpsrc, "src",
      base->src_probe);
  base->src_probe = 0UL;

  kms_rtp_base_connection_remove_probe (base, self->rtp_udpsink, "sink",
      base->sink_probe);
  base->sink_probe = 0UL;
}

static void
kms_srtp_connection_collect_latency_stats (KmsIRtpConnection * obj,
    gboolean enable)
{
  KmsRtpBaseConnection *base = KMS_RTP_BASE_CONNECTION (obj);

  KMS_RTP_BASE_CONNECTION_LOCK (base);

  if (enable) {
    kms_srtp_connection_enable_latency_stats (base);
  } else {
    kms_srtp_transport_disable_latency_notification (base);
  }

  kms_rtp_base_connection_collect_latency_stats (obj, enable);

  KMS_RTP_BASE_CONNECTION_UNLOCK (base);
}

static void
kms_srtp_connection_finalize (GObject * object)
{
  KmsSrtpConnection *self = KMS_SRTP_CONNECTION (object);

  GST_DEBUG_OBJECT (self, "finalize");

  kms_srtp_transport_disable_latency_notification (KMS_RTP_BASE_CONNECTION
      (self));

  g_clear_object (&self->rtp_udpsink);
  g_clear_object (&self->rtp_udpsrc);

  g_clear_object (&self->rtcp_udpsink);
  g_clear_object (&self->rtcp_udpsrc);

  g_clear_object (&self->srtpenc);
  g_clear_object (&self->srtpdec);

  kms_socket_finalize (&self->rtp_socket);
  kms_socket_finalize (&self->rtcp_socket);

  g_free (self->r_key);

  /* chain up */
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
kms_srtp_connection_init (KmsSrtpConnection * self)
{
  // self->priv = KMS_SRTP_CONNECTION_GET_PRIVATE (self);
   self->priv = kms_srtp_connection_get_instance_private (self);

  self->priv->connected = FALSE;

  g_type_interface_add_prerequisite (KMS_TYPE_SRTP_CONNECTION, KMS_TYPE_I_RTP_CONNECTION);
           g_type_add_interface_static (KMS_TYPE_SRTP_CONNECTION, KMS_TYPE_I_RTP_CONNECTION,
                                        &(const GInterfaceInfo) {
                                            (GInterfaceInitFunc) kms_srtp_connection_interface_init,
                                            NULL,
                                            NULL
                                        });
}

static void
kms_srtp_connection_class_init (KmsSrtpConnectionClass * klass)
{
  GObjectClass *gobject_class;
  KmsRtpBaseConnectionClass *base_conn_class;

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, GST_DEFAULT_NAME, 0,
      GST_DEFAULT_NAME);

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = kms_srtp_connection_finalize;
  gobject_class->get_property = kms_srtp_connection_get_property;
  gobject_class->set_property = kms_srtp_connection_set_property;

  base_conn_class = KMS_RTP_BASE_CONNECTION_CLASS (klass);
  base_conn_class->get_rtp_port = kms_srtp_connection_get_rtp_port;
  base_conn_class->get_rtcp_port = kms_srtp_connection_get_rtcp_port;
  base_conn_class->set_remote_info = kms_srtp_connection_set_remote_info;

  // g_type_class_add_private (klass, sizeof (KmsSrtpConnectionPrivate));

  g_object_class_override_property (gobject_class, PROP_ADDED, "added");
  g_object_class_override_property (gobject_class, PROP_CONNECTED, "connected");
  g_object_class_override_property (gobject_class, PROP_IS_CLIENT, "is-client");
  g_object_class_override_property (gobject_class, PROP_MAX_PORT, "max-port");
  g_object_class_override_property (gobject_class, PROP_MIN_PORT, "min-port");

  obj_signals[SIGNAL_KEY_SOFT_LIMIT] =
      g_signal_new ("key-soft-limit",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (KmsSrtpConnectionClass, key_soft_limit), NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}

void
kms_srtp_connection_set_key (KmsSrtpConnection * conn, const gchar * key,
    guint auth, guint cipher, gboolean local)
{
  g_return_if_fail (KMS_IS_SRTP_CONNECTION (conn));

  if (local) {
    GstBuffer *buff_key;
    guint8 *bin_buff;
    gsize len;

    bin_buff = g_base64_decode (key, &len);
    buff_key = gst_buffer_new_wrapped (bin_buff, len);

    g_object_set (conn->srtpenc, "key", buff_key, "rtp-cipher", cipher,
        "rtcp-cipher", cipher, "rtp-auth", auth, "rtcp-auth", auth, NULL);
    gst_buffer_unref (buff_key);
  } else {
    gboolean changed;

    KMS_RTP_BASE_CONNECTION_LOCK (conn);

    changed = !conn->r_key_set || g_strcmp0 (key, conn->r_key) != 0
        || conn->r_auth != auth || conn->r_cipher != cipher;

    if (changed) {
      g_free (conn->r_key);
      conn->r_key = g_strdup (key);
      conn->r_auth = auth;
      conn->r_cipher = cipher;
      conn->r_updated = TRUE;
      conn->r_key_set = TRUE;
    }

    KMS_RTP_BASE_CONNECTION_UNLOCK (conn);
  }
}

static void
kms_srtp_connection_interface_init (KmsIRtpConnectionInterface * iface)
{
  iface->add = kms_srtp_connection_add;
  iface->src_sync_state_with_parent =
      kms_srtp_connection_src_sync_state_with_parent;
  iface->sink_sync_state_with_parent =
      kms_srtp_connection_sink_sync_state_with_parent;
  iface->request_rtp_sink = kms_srtp_connection_request_rtp_sink;
  iface->request_rtp_src = kms_srtp_connection_request_rtp_src;
  iface->request_rtcp_sink = kms_srtp_connection_request_rtcp_sink;
  iface->request_rtcp_src = kms_srtp_connection_request_rtcp_src;
  iface->set_latency_callback = kms_rtp_base_connection_set_latency_callback;
  iface->collect_latency_stats = kms_srtp_connection_collect_latency_stats;
}
