#include "GstPlayer.h"
#include "Config.h"
#include "RtspTweak.h"

#include <gst/gst.h>
#include <gst/video/navigation.h>
#include <gst/video/videooverlay.h>

#include <QMap>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <gst/base/gstbasesink.h>
#include <mutex>

QSet<GstPlayer *> GstPlayer::s_all;
double GstPlayer::s_volume = 0.6;


struct GstPlayer::Shared {
    GstElement *pipe = nullptr;
    GstElement *dec = nullptr;
    GstElement *queue = nullptr;
    std::atomic<quint64> bytes{0};
    std::mutex m;
    QString decoder;
    // am thanh
    bool wantAudio = false;
    std::atomic<bool> hasAudio{false};     // camera co gui luong am thanh
    std::atomic<bool> audioLinked{false};  // da noi nhanh phat tieng
    GstElement *audioConv = nullptr;
    QString audioCodec;
    std::atomic<double> volume{0.6};
    // vi tri theo moc RTP video (dung khi tua nhanh: dong ho may khong phan anh thoi gian ban ghi)
    std::atomic<qint64> rtpFirst{-1};
    std::atomic<qint64> rtpLast{-1};   // da mo rong qua 32 bit
    // LITEPSS_HITCH=1 (chan doan): thoi diem ve uoc tinh tung khung (ns dong ho), do tre so voi han
    GstElement *sink = nullptr;
    std::vector<qint64> renderAt;
    std::vector<qint64> lateBy;
    // gian deu nhip (chi luong xu ly cua queue src cham vao)
    qint64 paceIn = -1, paceOut = -1;
    double paceD = 0;
    int paceN = 0;
};

// Gian deu nhip hien khung xem truc tiep.
// Camera 2K cua dau ghi chi chay 20 fps, lay 4/5 khung cua cam bien 25 Hz -> moc thoi gian 40-40-40-80 ms;
// ve dung moc thi cu 200 ms lai khung lai 80 ms (giat nhe). O day uoc luong khoang cach trung binh D va
// dat lai PTS = PTS truoc + D, keo dan ve moc that (sai lech toi da vai chuc ms, khong troi).
// Gap moc nhay lon (mat khung, dau ghi ngung gui) thi bam lai moc that ngay.
static GstPadProbeReturn onPace(GstPad *, GstPadProbeInfo *info, gpointer ud)
{
    auto *sh = static_cast<GstPlayer::Shared *>(ud);
    GstBuffer *b = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!b || !GST_BUFFER_PTS_IS_VALID(b)) return GST_PAD_PROBE_OK;
    const qint64 in = qint64(GST_BUFFER_PTS(b));
    qint64 out = in;
    if (sh->paceIn >= 0) {
        const qint64 dIn = in - sh->paceIn;
        if (dIn > 0 && dIn < 300 * GST_MSECOND) {
            const double a = sh->paceN < 30 ? 0.15 : 0.03;
            sh->paceD = sh->paceD > 0 ? sh->paceD * (1 - a) + dIn * a : double(dIn);
            ++sh->paceN;
            const qint64 pred = sh->paceOut + qint64(sh->paceD);
            const qint64 err = in - pred;
            if (sh->paceN >= 8 && std::llabs(err) < 120 * GST_MSECOND) out = pred + err / 8;
        }
    }
    sh->paceIn = in;
    sh->paceOut = out;
    if (out != in) {
        b = gst_buffer_make_writable(b);
        GST_BUFFER_PTS(b) = GstClockTime(out);
        GST_BUFFER_DTS(b) = GST_CLOCK_TIME_NONE;
        if (sh->paceD > 0) GST_BUFFER_DURATION(b) = GstClockTime(sh->paceD);
        GST_PAD_PROBE_INFO_DATA(info) = b;
    }
    return GST_PAD_PROBE_OK;
}

static qint64 s_t0 = 0;
static GstPadProbeReturn onSinkBuf(GstPad *, GstPadProbeInfo *info, gpointer ud)
{
    auto *sh = static_cast<GstPlayer::Shared *>(ud);
    GstBuffer *b = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!b || !GST_BUFFER_PTS_IS_VALID(b) || !sh->sink) return GST_PAD_PROBE_OK;
    GstClock *clk = gst_element_get_clock(sh->sink);
    if (!clk) return GST_PAD_PROBE_OK;
    const qint64 now = qint64(gst_clock_get_time(clk));
    gst_object_unref(clk);
    GstPad *pad = gst_element_get_static_pad(sh->sink, "sink");
    GstEvent *seg = gst_pad_get_sticky_event(pad, GST_EVENT_SEGMENT, 0);
    gst_object_unref(pad);
    if (!seg) return GST_PAD_PROBE_OK;
    const GstSegment *sg = nullptr;
    gst_event_parse_segment(seg, &sg);
    const qint64 rt = qint64(gst_segment_to_running_time(sg, GST_FORMAT_TIME, GST_BUFFER_PTS(b)));
    gst_event_unref(seg);
    const qint64 deadline = qint64(gst_element_get_base_time(sh->sink)) + rt +
                            qint64(gst_base_sink_get_latency(GST_BASE_SINK(sh->sink)));
    if (!s_t0) s_t0 = now;
    std::lock_guard<std::mutex> lk(sh->m);
    sh->renderAt.push_back(std::max(now, deadline));
    sh->lateBy.push_back(now - deadline);
    return GST_PAD_PROBE_OK;
}

static void trackRtp(GstPlayer::Shared *sh, GstBuffer *b)
{
    guint8 hdr[8];
    if (gst_buffer_extract(b, 0, hdr, 8) != 8 || (hdr[0] >> 6) != 2) return;
    const quint32 ts = (quint32(hdr[4]) << 24) | (quint32(hdr[5]) << 16) | (quint32(hdr[6]) << 8) | hdr[7];
    const qint64 last = sh->rtpLast;
    qint64 ext = ts;
    if (last >= 0) {
        // mo rong 32 bit: chon gia tri gan last nhat
        ext = (last & ~qint64(0xffffffff)) | ts;
        if (ext - last > 0x80000000LL) ext -= 0x100000000LL;
        else if (last - ext > 0x80000000LL) ext += 0x100000000LL;
    }
    if (sh->rtpFirst < 0) sh->rtpFirst = ext;
    if (ext > last) sh->rtpLast = ext;
}

static GstPadProbeReturn onVideoRtp(GstPad *, GstPadProbeInfo *info, gpointer ud)
{
    auto *sh = static_cast<GstPlayer::Shared *>(ud);
    if (info->type & GST_PAD_PROBE_TYPE_BUFFER) {
        trackRtp(sh, GST_PAD_PROBE_INFO_BUFFER(info));
    } else if (info->type & GST_PAD_PROBE_TYPE_BUFFER_LIST) {
        GstBufferList *l = GST_PAD_PROBE_INFO_BUFFER_LIST(info);
        const guint n = gst_buffer_list_length(l);
        for (guint i = 0; i < n; ++i) trackRtp(sh, gst_buffer_list_get(l, i));
    }
    return GST_PAD_PROBE_OK;
}

// Chi noi nhanh am thanh khi chac chan co depay + decoder, tranh loi "thieu plugin" lam dung ca o.
static bool audioSupported(const gchar *enc)
{
    if (!enc) return false;
    static const char *ok[] = {"PCMA", "PCMU", "MPEG4-GENERIC", "MP4A-LATM", "G726", "G726-16", "G726-24",
                               "G726-32", "G726-40", "L16", "OPUS"};
    for (const char *e : ok)
        if (g_ascii_strcasecmp(enc, e) == 0) return true;
    return false;
}

static void onAudioDecPadAdded(GstElement *, GstPad *pad, gpointer ud)
{
    auto *sh = static_cast<GstPlayer::Shared *>(ud);
    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (!caps) caps = gst_pad_query_caps(pad, nullptr);
    const gchar *name = (caps && gst_caps_get_size(caps) > 0)
                            ? gst_structure_get_name(gst_caps_get_structure(caps, 0)) : "";
    if (g_str_has_prefix(name, "audio/")) {
        GstPad *s = gst_element_get_static_pad(sh->audioConv, "sink");
        if (!gst_pad_is_linked(s) && gst_pad_link(pad, s) == GST_PAD_LINK_OK) sh->audioLinked = true;
        gst_object_unref(s);
    }
    if (caps) gst_caps_unref(caps);
}

static void addFakeSink(GstPlayer::Shared *sh, GstPad *pad)
{
    GstElement *fs = gst_element_factory_make("fakesink", nullptr);
    g_object_set(fs, "sync", FALSE, "async", FALSE, nullptr);
    gst_bin_add(GST_BIN(sh->pipe), fs);
    gst_element_sync_state_with_parent(fs);
    GstPad *fsPad = gst_element_get_static_pad(fs, "sink");
    gst_pad_link(pad, fsPad);
    gst_object_unref(fsPad);
}

// rtp (audio) -> decodebin -> audioconvert -> audioresample -> autoaudiosink
static bool addAudioBranch(GstPlayer::Shared *sh, GstPad *pad)
{
    GstElement *adec = gst_element_factory_make("decodebin", nullptr);
    GstElement *conv = gst_element_factory_make("audioconvert", nullptr);
    GstElement *res = gst_element_factory_make("audioresample", nullptr);
    GstElement *vol = gst_element_factory_make("volume", "audiovol");
    GstElement *q = gst_element_factory_make("queue", nullptr);
    GstElement *sink = gst_element_factory_make("autoaudiosink", nullptr);
    if (!adec || !conv || !res || !vol || !q || !sink) {
        for (GstElement *e : {adec, conv, res, vol, q, sink})
            if (e) gst_object_unref(e);
        return false;
    }
    sh->audioConv = conv;
    g_object_set(vol, "volume", sh->volume.load(), nullptr);
    gst_bin_add_many(GST_BIN(sh->pipe), adec, conv, res, vol, q, sink, nullptr);
    gst_element_link_many(conv, res, vol, q, sink, nullptr);
    g_signal_connect(adec, "pad-added", G_CALLBACK(onAudioDecPadAdded), sh);
    for (GstElement *e : {sink, q, vol, res, conv, adec}) gst_element_sync_state_with_parent(e);
    GstPad *s = gst_element_get_static_pad(adec, "sink");
    const bool ok = gst_pad_link(pad, s) == GST_PAD_LINK_OK;
    gst_object_unref(s);
    return ok;
}

// ---------- callback chay tren streaming thread cua GStreamer ----------

static GstPadProbeReturn countBytes(GstPad *, GstPadProbeInfo *info, gpointer ud)
{
    auto *sh = static_cast<GstPlayer::Shared *>(ud);
    if (info->type & GST_PAD_PROBE_TYPE_BUFFER) {
        sh->bytes += gst_buffer_get_size(GST_PAD_PROBE_INFO_BUFFER(info));
    } else if (info->type & GST_PAD_PROBE_TYPE_BUFFER_LIST) {
        GstBufferList *l = GST_PAD_PROBE_INFO_BUFFER_LIST(info);
        const guint n = gst_buffer_list_length(l);
        for (guint i = 0; i < n; ++i) sh->bytes += gst_buffer_get_size(gst_buffer_list_get(l, i));
    }
    return GST_PAD_PROBE_OK;
}

static void onSrcPadAdded(GstElement *, GstPad *pad, gpointer ud)
{
    auto *sh = static_cast<GstPlayer::Shared *>(ud);
    gst_pad_add_probe(pad, GstPadProbeType(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST),
                      countBytes, sh, nullptr);

    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (!caps) caps = gst_pad_query_caps(pad, nullptr);
    const gchar *media = nullptr;
    const gchar *enc = nullptr;
    if (caps && gst_caps_get_size(caps) > 0) {
        GstStructure *st = gst_caps_get_structure(caps, 0);
        media = gst_structure_get_string(st, "media");
        enc = gst_structure_get_string(st, "encoding-name");
    }

    GstPad *decSink = gst_element_get_static_pad(sh->dec, "sink");
    const bool isVideo = media && g_str_equal(media, "video");
    const bool isAudio = media && g_str_equal(media, "audio");
    if (isAudio) {
        sh->hasAudio = true;
        {
            std::lock_guard<std::mutex> lk(sh->m);
            sh->audioCodec = QString::fromUtf8(enc ? enc : "?");
        }
    }
    if (isVideo && !gst_pad_is_linked(decSink)) {
        gst_pad_add_probe(pad, GstPadProbeType(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST),
                          onVideoRtp, sh, nullptr);
        gst_pad_link(pad, decSink);
    } else if (isAudio && sh->wantAudio && !sh->audioConv && audioSupported(enc) && addAudioBranch(sh, pad)) {
        // da noi nhanh am thanh
    } else {
        // am thanh khong bat / metadata / luong video thu hai: bo vao fakesink
        addFakeSink(sh, pad);
    }
    gst_object_unref(decSink);
    if (caps) gst_caps_unref(caps);
}

static void onDecPadAdded(GstElement *, GstPad *pad, gpointer ud)
{
    auto *sh = static_cast<GstPlayer::Shared *>(ud);
    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (!caps) caps = gst_pad_query_caps(pad, nullptr);
    const gchar *name = (caps && gst_caps_get_size(caps) > 0)
                            ? gst_structure_get_name(gst_caps_get_structure(caps, 0)) : "";
    if (g_str_has_prefix(name, "video/")) {
        GstPad *qSink = gst_element_get_static_pad(sh->queue, "sink");
        if (!gst_pad_is_linked(qSink)) gst_pad_link(pad, qSink);
        gst_object_unref(qSink);
    }
    if (caps) gst_caps_unref(caps);
}

static void onDeepElementAdded(GstBin *, GstBin *, GstElement *el, gpointer ud)
{
    auto *sh = static_cast<GstPlayer::Shared *>(ud);
    GstElementFactory *f = gst_element_get_factory(el);
    if (!f) return;
    const gchar *klass = gst_element_factory_get_metadata(f, GST_ELEMENT_METADATA_KLASS);
    if (klass && strstr(klass, "Decoder") && strstr(klass, "Video")) {
        std::lock_guard<std::mutex> lk(sh->m);
        sh->decoder = QString::fromUtf8(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(f)));
    }
}

// ---------- GstPlayer ----------

GstPlayer::GstPlayer(QObject *parent) : QObject(parent)
{
    m_busTimer.setInterval(40);
    connect(&m_busTimer, &QTimer::timeout, this, &GstPlayer::pollBus);
    m_rateTimer.setInterval(1000);
    connect(&m_rateTimer, &QTimer::timeout, this, &GstPlayer::updateRate);
}

GstPlayer::~GstPlayer()
{
    stop();
}

static GstElement *makeSink(QString *chosen)
{
    QStringList candidates;
    if (!cfg().videoSink.isEmpty()) candidates << cfg().videoSink;
#ifdef _WIN32
    // Do tren Intel HD 620, 16 luong phu H.265: d3d12videosink ~337 MB RAM, d3d11videosink ~687 MB
    // (bo giai ma mac dinh la d3d12h265dec, cung API thi khong phai chuyen doi giua D3D11/D3D12).
    candidates << "d3d12videosink" << "d3d11videosink" << "glimagesink";
#else
    candidates << "glimagesink" << "xvimagesink" << "ximagesink";
#endif
    for (const QString &c : candidates) {
        if (GstElement *e = gst_element_factory_make(c.toUtf8().constData(), "sink")) {
            *chosen = c;
            return e;
        }
    }
    return nullptr;
}

static bool hasProp(GstElement *e, const char *name)
{
    return g_object_class_find_property(G_OBJECT_GET_CLASS(e), name) != nullptr;
}

bool GstPlayer::open(const StreamParams &p, quintptr windowHandle)
{
    stop();

    m_sh = new Shared;
    m_pipe = gst_pipeline_new(nullptr);
    GstElement *src = gst_element_factory_make("rtspsrc", "src");
    GstElement *dec = gst_element_factory_make("decodebin", "dec");
    GstElement *queue = gst_element_factory_make("queue", "q");
    m_sink = makeSink(&m_sinkName);
    if (!src || !dec || !queue || !m_sink) {
        const QString missing = !src ? "rtspsrc" : !dec ? "decodebin" : !queue ? "queue" : "video sink";
        for (GstElement *e : {src, dec, queue, m_sink})
            if (e) gst_object_unref(e);
        gst_object_unref(m_pipe);
        m_pipe = nullptr;
        m_sink = nullptr;
        delete m_sh;
        m_sh = nullptr;
        emit error(QStringLiteral("Thiáº¿u plugin GStreamer: %1").arg(missing));
        return false;
    }
    m_sh->pipe = m_pipe;
    m_sh->dec = dec;
    m_sh->queue = queue;
    m_sh->wantAudio = p.audio;
    m_sh->volume = s_volume;

    g_object_set(src, "location", p.url.toUtf8().constData(), "latency", guint(p.latencyMs), nullptr);
    gst_util_set_object_arg(G_OBJECT(src), "protocols", "tcp");
    if (!p.user.isEmpty())
        g_object_set(src, "user-id", p.user.toUtf8().constData(),
                     "user-pw", p.password.toUtf8().constData(), nullptr);
    if (hasProp(src, "tcp-timeout")) g_object_set(src, "tcp-timeout", guint64(8000000), nullptr);
    // Tua nhanh xem lai: header "Speed: N" -> dau ghi gui du khung nhanh gap N (do: x8 = 153 fps 1440p,
    // GPU HD 620 giai ma 27%). "Scale" chi gui khung chinh (~2 fps o x8) nen khong dung.
    if (p.speed > 1.01) {
        const QByteArray v = QByteArray::number(p.speed, 'f', 1);
        RtspTweak::attach(src, "Speed", v.constData());
    }
    if (hasProp(src, "teardown-timeout"))
        g_object_set(src, "teardown-timeout", guint64(200 * GST_MSECOND), nullptr);

    // Ban dau dung queue 3 khung + leaky: khi mang dao dong, khung ve don cuc -> queue day -> vut khung
    // -> hinh nhay coc. Gio de queue khong vut khung; do tre do bo dem jitter cua rtspsrc (latency) lo.
    g_object_set(queue, "max-size-buffers", guint(p.live ? 10 : 30), "max-size-bytes", guint(0),
                 "max-size-time", guint64(0), nullptr);
    if (p.live && p.leaky) gst_util_set_object_arg(G_OBJECT(queue), "leaky", "downstream");

    // Khi tua nhanh, dau ghi da gui dung nhip -> ve ngay khi nhan (moc thoi gian RTP chay nhanh gap N lan)
    g_object_set(m_sink, "sync", gboolean(p.sync && p.speed <= 1.01), nullptr);
    // Hinh tran day o (nguoi dung yeu cau "Tràn đầy ô"): keo gian theo o, khong giu ti le / khong vien den
    if (hasProp(m_sink, "force-aspect-ratio")) g_object_set(m_sink, "force-aspect-ratio", FALSE, nullptr);
    // Thu phong chat luong cao (hinh 704x576 phong len o to bot rang cua)
    if (hasProp(m_sink, "sampling-method")) gst_util_set_object_arg(G_OBJECT(m_sink), "sampling-method", "anisotropic");
    applyColor();
    // d3d12videosink mac dinh vut moi khung tre qua 5 ms -> mang/dau ghi cham mot nhip la hinh giat.
    // Ve tre van hon vut (vut = dung hinh + nhay coc). Gioi han 1 giay de neu may qua tai thi van bam
    // thoi gian thuc thay vi tre dan.
    if (p.live && hasProp(m_sink, "max-lateness"))
        g_object_set(m_sink, "max-lateness", gint64(1000 * GST_MSECOND), nullptr);
    // Gian deu nhip (xem truc tiep, khong tua). LITEPSS_PACING=0/1 ghi de cau hinh de so sanh.
    {
        const QByteArray ev = qgetenv("LITEPSS_PACING");
        const bool pace = ev.isEmpty() ? cfg().liveSmoothPacing : ev != "0";
        if (p.live && p.sync && p.speed <= 1.01 && pace) {
            GstPad *qp = gst_element_get_static_pad(queue, "src");
            gst_pad_add_probe(qp, GST_PAD_PROBE_TYPE_BUFFER, onPace, m_sh, nullptr);
            gst_object_unref(qp);
        }
    }
    // LITEPSS_HITCH=1: do do deu nhip ve (them vao --stats), chi de chan doan
    if (qgetenv("LITEPSS_HITCH") == "1") {
        m_sh->sink = m_sink;
        GstPad *sp = gst_element_get_static_pad(m_sink, "sink");
        gst_pad_add_probe(sp, GST_PAD_PROBE_TYPE_BUFFER, onSinkBuf, m_sh, nullptr);
        gst_object_unref(sp);
    }
    gst_bin_add_many(GST_BIN(m_pipe), src, dec, queue, m_sink, nullptr);
    gst_element_link(queue, m_sink);

    if (GST_IS_VIDEO_OVERLAY(m_sink))
        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(m_sink), guintptr(windowHandle));

    g_signal_connect(src, "pad-added", G_CALLBACK(onSrcPadAdded), m_sh);
    g_signal_connect(dec, "pad-added", G_CALLBACK(onDecPadAdded), m_sh);
    g_signal_connect(m_pipe, "deep-element-added", G_CALLBACK(onDeepElementAdded), m_sh);

    m_bus = gst_element_get_bus(m_pipe);
    m_playing = false;
    m_lastBytes = 0;
    m_kbps = 0;
    m_fps = 0;
    m_rendered = m_dropped = m_lastRendered = 0;
    m_sessionClock.invalidate();
    m_rateClock.start();
    m_busTimer.start();
    m_rateTimer.start();
    s_all.insert(this);

    if (gst_element_set_state(m_pipe, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        emit error(QStringLiteral("KhÃ´ng chuyá»ƒn Ä‘Æ°á»£c sang PLAYING"));
        return false;
    }
    return true;
}

void GstPlayer::stop()
{
    m_busTimer.stop();
    m_rateTimer.stop();
    s_all.remove(this);
    if (m_pipe) {
        gst_element_set_state(m_pipe, GST_STATE_NULL);
        gst_object_unref(m_pipe);
        m_pipe = nullptr;
        m_sink = nullptr;
    }
    if (m_bus) {
        gst_object_unref(m_bus);
        m_bus = nullptr;
    }
    delete m_sh;
    m_sh = nullptr;
    m_playing = false;
    m_kbps = 0;
}

void GstPlayer::setPaused(bool paused)
{
    if (m_pipe) gst_element_set_state(m_pipe, paused ? GST_STATE_PAUSED : GST_STATE_PLAYING);
}

bool GstPlayer::setRate(double rate)
{
    if (!m_pipe) return false;
    const auto flags = GstSeekFlags(GST_SEEK_FLAG_FLUSH);
    if (gst_element_seek(m_pipe, rate, GST_FORMAT_TIME, flags, GST_SEEK_TYPE_NONE, 0,
                         GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE))
        return true;
    gint64 pos = 0;
    if (!gst_element_query_position(m_pipe, GST_FORMAT_TIME, &pos)) return false;
    return gst_element_seek(m_pipe, rate, GST_FORMAT_TIME, flags, GST_SEEK_TYPE_SET, pos,
                            GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
}

void GstPlayer::setColor(const ColorAdjust &c)
{
    m_color = c;
    applyColor();
}

void GstPlayer::applyColor()
{
    if (!m_sink) return;
    if (hasProp(m_sink, "brightness")) g_object_set(m_sink, "brightness", m_color.brightness, nullptr);
    if (hasProp(m_sink, "contrast")) g_object_set(m_sink, "contrast", m_color.contrast, nullptr);
    if (hasProp(m_sink, "saturation")) g_object_set(m_sink, "saturation", m_color.saturation, nullptr);
    if (hasProp(m_sink, "hue")) g_object_set(m_sink, "hue", m_color.hue, nullptr);
}

bool GstPlayer::colorSupported() const
{
    return m_sink && hasProp(m_sink, "saturation");
}

bool GstPlayer::hasAudioStream() const
{
    return m_sh && m_sh->hasAudio;
}

bool GstPlayer::audioActive() const
{
    return m_sh && m_sh->audioLinked;
}

QString GstPlayer::audioCodec() const
{
    if (!m_sh) return {};
    std::lock_guard<std::mutex> lk(m_sh->m);
    return m_sh->audioCodec;
}

void GstPlayer::setSync(bool sync)
{
    if (m_sink) g_object_set(m_sink, "sync", gboolean(sync), nullptr);
}

qint64 GstPlayer::positionMs() const
{
    gint64 pos = 0;
    if (m_pipe && gst_element_query_position(m_pipe, GST_FORMAT_TIME, &pos) && pos >= 0)
        return pos / GST_MSECOND;
    return -1;
}

bool GstPlayer::hasRendered() const
{
    if (!m_sink || !hasProp(m_sink, "stats")) return m_playing;
    GstStructure *st = nullptr;
    g_object_get(m_sink, "stats", &st, nullptr);
    guint64 r = 0;
    if (st) {
        gst_structure_get_uint64(st, "rendered", &r);
        gst_structure_free(st);
    }
    return r > 0;
}

qint64 GstPlayer::mediaPositionMs() const
{
    if (!m_sh) return -1;
    const qint64 a = m_sh->rtpFirst, b = m_sh->rtpLast;
    if (a < 0 || b < a) return -1;
    return (b - a) / 90; // dong ho RTP video 90 kHz
}

void GstPlayer::expose()
{
    if (m_sink && GST_IS_VIDEO_OVERLAY(m_sink)) gst_video_overlay_expose(GST_VIDEO_OVERLAY(m_sink));
}

QString GstPlayer::decoderName() const
{
    if (!m_sh) return {};
    std::lock_guard<std::mutex> lk(m_sh->m);
    return m_sh->decoder;
}

QString GstPlayer::resolution() const
{
    if (!m_sink) return {};
    GstPad *pad = gst_element_get_static_pad(m_sink, "sink");
    QString r;
    if (GstCaps *caps = gst_pad_get_current_caps(pad)) {
        int w = 0, h = 0;
        GstStructure *s = gst_caps_get_structure(caps, 0);
        if (gst_structure_get_int(s, "width", &w) && gst_structure_get_int(s, "height", &h))
            r = QStringLiteral("%1x%2").arg(w).arg(h);
        gst_caps_unref(caps);
    }
    gst_object_unref(pad);
    return r;
}

void GstPlayer::updateRate()
{
    if (!m_sh) return;
    const quint64 b = m_sh->bytes.load();
    const double sec = m_rateClock.restart() / 1000.0;
    if (sec > 0) m_kbps = (b - m_lastBytes) * 8.0 / 1000.0 / sec;
    m_lastBytes = b;

    // Thong ke cua video sink: so khung da ve / bi bo (do den tre)
    if (m_sink && hasProp(m_sink, "stats")) {
        GstStructure *st = nullptr;
        g_object_get(m_sink, "stats", &st, nullptr);
        if (st) {
            guint64 r = 0, d = 0;
            gst_structure_get_uint64(st, "rendered", &r);
            gst_structure_get_uint64(st, "dropped", &d);
            gst_structure_free(st);
            if (r > 0 && !m_sessionClock.isValid()) m_sessionClock.start();
            if (sec > 0) m_fps = (r - m_lastRendered) / sec;
            m_lastRendered = r;
            m_rendered = r;
            m_dropped = d;
        }
    }
}

QString GstPlayer::statsSummary()
{
    QStringList lines;
    int i = 0;
    for (GstPlayer *p : s_all) {
        const double secs = p->m_sessionClock.isValid() ? p->m_sessionClock.elapsed() / 1000.0 : 0;
        QString hitch;
        if (p->m_sh && qgetenv("LITEPSS_HITCH") == "1") {
            std::vector<qint64> ra, lb;
            { std::lock_guard<std::mutex> lk(p->m_sh->m); ra = p->m_sh->renderAt; lb = p->m_sh->lateBy; }
            // bo 3 giay dau (khoi dong)
            std::vector<qint64> iv;
            for (size_t k = 1; k < ra.size(); ++k) if (ra[k] - ra.front() > 3 * GST_SECOND) iv.push_back(ra[k] - ra[k - 1]);
            if (iv.size() > 20) {
                std::vector<qint64> sorted = iv;
                std::sort(sorted.begin(), sorted.end());
                const qint64 med = sorted[sorted.size() / 2];
                int big = 0; double sum = 0, sq = 0;
                for (qint64 x : iv) { if (x > med * 18 / 10) ++big; sum += x; sq += double(x) * x; }
                const double mean = sum / iv.size(), sd = std::sqrt(std::max(0.0, sq / iv.size() - mean * mean));
                std::vector<qint64> l2 = lb; std::sort(l2.begin(), l2.end());
                int late = 0; for (qint64 x : lb) if (x > 0) ++late;
                QMap<int, int> hist;
                for (qint64 x : iv) hist[int((x + 5 * GST_MSECOND) / (10 * GST_MSECOND)) * 10]++;
                QList<QPair<int, int>> hv;
                for (auto it = hist.begin(); it != hist.end(); ++it) hv << qMakePair(it.value(), it.key());
                std::sort(hv.begin(), hv.end(), [](auto &x, auto &y) { return x.first > y.first; });
                QStringList hs;
                for (int k = 0; k < std::min<int>(5, hv.size()); ++k) hs << QStringLiteral("%1ms:%2").arg(hv[k].second).arg(hv[k].first);
                hitch = QStringLiteral(" [%1]").arg(hs.join(' '));
                QStringList gaps;
                for (size_t k = 1; k < ra.size(); ++k)
                    if (ra[k] - ra[k - 1] > 300 * GST_MSECOND && ra[k] - ra.front() > 3 * GST_SECOND)
                        gaps << QStringLiteral("%1s+%2").arg((ra[k - 1] - ra.front()) / 1e9 + (ra.front() - s_t0) / 1e9, 0, 'f', 1)
                                                        .arg((ra[k] - ra[k - 1]) / 1000000);
                hitch += QStringLiteral(" GAPS{%1}").arg(gaps.join(' '));
                hitch += QStringLiteral(" med=%1ms sd=%2ms giat=%3 tre=%4/%5 p99tre=%6ms")
                            .arg(med / 1e6, 0, 'f', 1).arg(sd / 1e6, 0, 'f', 1).arg(big).arg(late).arg(lb.size())
                            .arg(l2[l2.size() * 99 / 100] / 1e6, 0, 'f', 0);
            }
        }
        lines << QStringLiteral("luong %1: fps_tb=%2 ve=%3 rot=%4 kbps=%5 dec=%6")
                     .arg(++i)
                     .arg(secs > 0 ? p->m_rendered / secs : 0.0, 0, 'f', 1)
                     .arg(p->m_rendered).arg(p->m_dropped).arg(qRound(p->m_kbps)).arg(p->decoderName()) + hitch;
    }
    return lines.join('\n');
}

double GstPlayer::totalKbps()
{
    double t = 0;
    for (GstPlayer *p : s_all) t += p->kbps();
    return t;
}

int GstPlayer::activeCount()
{
    return s_all.size();
}

void GstPlayer::pollBus()
{
    // Dung con tro cuc bo: emit co the dan toi stop() ngay trong vong lap.
    while (m_bus) {
        GstMessage *msg = gst_bus_pop(m_bus);
        if (!msg) break;
        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError *err = nullptr;
            gchar *dbg = nullptr;
            gst_message_parse_error(msg, &err, &dbg);
            const QString text = QString::fromUtf8(err ? err->message : "lá»—i khÃ´ng rÃµ");
            g_clear_error(&err);
            g_free(dbg);
            gst_message_unref(msg);
            emit error(text);
            return;
        }
        case GST_MESSAGE_EOS:
            gst_message_unref(msg);
            emit endOfStream();
            return;
        case GST_MESSAGE_STATE_CHANGED:
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(m_pipe)) {
                GstState oldS, newS, pending;
                gst_message_parse_state_changed(msg, &oldS, &newS, &pending);
                if (newS == GST_STATE_PLAYING && !m_playing) {
                    m_playing = true;
                    gst_message_unref(msg);
                    emit playing();
                    continue;
                }
            }
            break;
        case GST_MESSAGE_ELEMENT:
            if (gst_navigation_message_get_type(msg) == GST_NAVIGATION_MESSAGE_EVENT) {
                GstEvent *ev = nullptr;
                if (gst_navigation_message_parse_event(msg, &ev) && ev) {
                    gint button = 0;
                    gdouble mx = 0, my = 0;
                    if (gst_navigation_event_get_type(ev) == GST_NAVIGATION_EVENT_MOUSE_BUTTON_PRESS &&
                        gst_navigation_event_parse_mouse_button_event(ev, &button, &mx, &my) && button == 3) {
                        gst_event_unref(ev);
                        gst_message_unref(msg);
                        emit videoContextMenu();
                        continue;
                    }
                    if (gst_navigation_event_get_type(ev) == GST_NAVIGATION_EVENT_MOUSE_BUTTON_PRESS) {
                        const bool dbl = m_lastClick.isValid() && m_lastClick.elapsed() < 400;
                        m_lastClick.start();
                        gst_event_unref(ev);
                        gst_message_unref(msg);
                        emit videoClicked(dbl);
                        continue;
                    }
                    gst_event_unref(ev);
                }
            }
            break;
        default:
            break;
        }
        gst_message_unref(msg);
    }
}

void GstPlayer::setGlobalVolume(double v)
{
    s_volume = v;
    for (GstPlayer *p : s_all) {
        if (!p->m_pipe) continue;
        p->m_sh->volume = v;
        if (GstElement *vol = gst_bin_get_by_name(GST_BIN(p->m_pipe), "audiovol")) {
            g_object_set(vol, "volume", v, nullptr);
            gst_object_unref(vol);
        }
    }
}
