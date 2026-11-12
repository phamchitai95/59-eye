#pragma once
// Them header tuy chon vao lenh PLAY cua rtspsrc (tin hieu "before-send"), dung cho dau ghi Dahua:
//   "Speed: N"         -> tua nhanh, gui du khung (bang thong x N) - dung cho xem lai
//   "Scale: N"         -> tua nhanh, chi khung chinh (~2 fps o x8, khong dung)
//   "Rate-Control: no" -> gui nhanh het muc mang cho phep (trich MP4)
// Do thu tren DH-NVR5232-4KS2 (FW 3.216): ca 3 deu chay; GStreamer seek(rate) thi dau ghi bo qua.
//
// Ham gst_rtsp_message_* nam trong libgstrtsp (rtspsrc da nap san). Lay dia chi luc chay de khong phai
// them thu vien vao CMake.
#include <gst/gst.h>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace RtspTweak {

struct Headers {
    char name[32] = {0};
    char value[32] = {0};
    char name2[32] = {0};
    char value2[32] = {0};
};

using ParseRequestFn = int (*)(void *msg, int *method, const char **uri, int *version);
using AddHeaderFn = int (*)(void *msg, const char *name, const char *value);

inline void *rtspSymbol(const char *name)
{
#ifdef _WIN32
    HMODULE m = GetModuleHandleA("libgstrtsp-1.0-0.dll");
    if (!m) m = LoadLibraryA("libgstrtsp-1.0-0.dll");
    return m ? reinterpret_cast<void *>(GetProcAddress(m, name)) : nullptr;
#else
    static void *h = dlopen("libgstrtsp-1.0.so.0", RTLD_NOW | RTLD_GLOBAL);
    return h ? dlsym(h, name) : nullptr;
#endif
}

// GstRTSPMethod: PLAY = 1 << 5
static constexpr int kMethodPlay = 1 << 5;

inline gboolean onBeforeSend(GstElement *, gpointer msg, gpointer ud)
{
    static auto parse = reinterpret_cast<ParseRequestFn>(rtspSymbol("gst_rtsp_message_parse_request"));
    static auto add = reinterpret_cast<AddHeaderFn>(rtspSymbol("gst_rtsp_message_add_header_by_name"));
    auto *h = static_cast<Headers *>(ud);
    if (!parse || !add || !msg) return TRUE;
    int method = 0, version = 0;
    const char *uri = nullptr;
    if (parse(msg, &method, &uri, &version) != 0 /*GST_RTSP_OK*/ || method != kMethodPlay) return TRUE;
    if (h->name[0]) add(msg, h->name, h->value);
    if (h->name2[0]) add(msg, h->name2, h->value2);
    return TRUE;
}

// Gan vao rtspsrc. Headers phai song lau hon rtspsrc (giai phong bang destroy notify).
inline bool attach(GstElement *rtspsrc, const char *name, const char *value, const char *name2 = nullptr,
                   const char *value2 = nullptr)
{
    if (!g_signal_lookup("before-send", G_OBJECT_TYPE(rtspsrc))) return false;
    if (!rtspSymbol("gst_rtsp_message_add_header_by_name")) return false;
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
