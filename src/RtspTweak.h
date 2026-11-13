#pragma once
// Them header tuy chon vao lenh PLAY cua rtspsrc (tin hieu "before-send"), dung cho dau ghi Dahua:
//   "Speed: N"         -> tua nhanh, gui du khung (bang thong x N) - dung cho xem lai
//   "Scale: N"         -> tua nhanh, chi khung chinh (~2 fps o x8, khong dung)
//   "Rate-Control: no" -> gui nhanh het muc mang cho phep (trich MP4)
// Do thu tren DH-NVR5232-4KS2 (FW 3.216): ca 3 deu chay; GStreamer seek(rate) thi dau ghi bo qua.
//
// Ham gst_rtsp_message_* lay tu libgstrtsp (link luc build qua gstreamer-rtsp-1.0).
#include <gst/gst.h>
// rtsp.h keo theo gio; gio co truong ten "signals" trung voi macro cua Qt -> tam bo macro khi include
#pragma push_macro("signals")
#undef signals
#include <gst/rtsp/rtsp.h>
#pragma pop_macro("signals")
#include <cstring>

namespace RtspTweak {

struct Headers {
    char name[32] = {0};
    char value[32] = {0};
    char name2[32] = {0};
    char value2[32] = {0};
};

inline gboolean onBeforeSend(GstElement *, GstRTSPMessage *msg, gpointer ud)
{
    auto *h = static_cast<Headers *>(ud);
    if (!msg || gst_rtsp_message_get_type(msg) != GST_RTSP_MESSAGE_REQUEST) return TRUE;
    GstRTSPMethod method = GST_RTSP_INVALID;
    const gchar *uri = nullptr;
    GstRTSPVersion version = GST_RTSP_VERSION_INVALID;
    if (gst_rtsp_message_parse_request(msg, &method, &uri, &version) != GST_RTSP_OK || method != GST_RTSP_PLAY)
        return TRUE;
    if (h->name[0]) gst_rtsp_message_add_header_by_name(msg, h->name, h->value);
    if (h->name2[0]) gst_rtsp_message_add_header_by_name(msg, h->name2, h->value2);
    return TRUE;
}

// Gan vao rtspsrc. Headers phai song lau hon rtspsrc (giai phong bang destroy notify).
inline bool attach(GstElement *rtspsrc, const char *name, const char *value, const char *name2 = nullptr,
                   const char *value2 = nullptr)
{
    if (!g_signal_lookup("before-send", G_OBJECT_TYPE(rtspsrc))) return false;
    auto *h = new Headers;
    std::strncpy(h->name, name, sizeof(h->name) - 1);
    std::strncpy(h->value, value, sizeof(h->value) - 1);
    if (name2) std::strncpy(h->name2, name2, sizeof(h->name2) - 1);
    if (value2) std::strncpy(h->value2, value2, sizeof(h->value2) - 1);
    g_signal_connect_data(rtspsrc, "before-send", G_CALLBACK(onBeforeSend), h,
                          [](gpointer d, GClosure *) { delete static_cast<Headers *>(d); }, GConnectFlags(0));
    return true;
}

} // namespace RtspTweak
