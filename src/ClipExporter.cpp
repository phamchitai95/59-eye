#include "ClipExporter.h"
#include "AppInfo.h"
#include <QDateTime>

#include <QFile>
#include <QFileInfo>
#include <gst/gst.h>

#include <atomic>
#include <mutex>
#include <vector>

struct ClipExporter::Shared {
    GstElement *pipe = nullptr;
    GstElement *mux = nullptr;
    std::mutex m;
    std::vector<GstPad *> queueSinks; // de day EOS khi du thoi luong
    bool hasVideo = false;
    bool wantAudio = false;
    bool audioAdded = false;
    QString sdpAudio;          // ma hoa am thanh khai bao trong SDP (neu ho tro)
    GstPad *audioMuxPad = nullptr; // xin truoc cung luc voi video (mp4mux khong cho xin pad sau khi da chay)
    qint64 durationNs = 0;
    std::atomic<qint64> firstPts{-1};
    std::atomic<qint64> lastPts{-1};
    std::atomic<bool> reachedEnd{false};
    QString videoCodec;
};

static GstElement *make(const char *f)
{
    return gst_element_factory_make(f, nullptr);
}

static GstClockTime bufTime(GstPadProbeInfo *info)
{
    GstBuffer *b = GST_PAD_PROBE_INFO_BUFFER(info);
    GstClockTime t = GST_BUFFER_PTS(b);
    if (!GST_CLOCK_TIME_IS_VALID(t)) t = GST_BUFFER_DTS(b);
    return t;
}

// Video: ghi nhan moc thoi gian, bo khung vuot qua thoi luong can trich (cat dung do dai)
static GstPadProbeReturn trackPts(GstPad *, GstPadProbeInfo *info, gpointer ud)
{
    auto *sh = static_cast<ClipExporter::Shared *>(ud);
    if (!(info->type & GST_PAD_PROBE_TYPE_BUFFER)) return GST_PAD_PROBE_OK;
    const GstClockTime t = bufTime(info);
    if (!GST_CLOCK_TIME_IS_VALID(t)) return GST_PAD_PROBE_OK;
    qint64 expected = -1;
    sh->firstPts.compare_exchange_strong(expected, qint64(t));
    if (qint64(t) - sh->firstPts > sh->durationNs) {
        sh->reachedEnd = true;
        return GST_PAD_PROBE_DROP;
    }
    if (qint64(t) > sh->lastPts) sh->lastPts = qint64(t);
    return GST_PAD_PROBE_OK;
}

// Am thanh: bo phan vuot qua thoi luong (tinh theo moc video)
static GstPadProbeReturn trimAudio(GstPad *, GstPadProbeInfo *info, gpointer ud)
{
    auto *sh = static_cast<ClipExporter::Shared *>(ud);
    if (!(info->type & GST_PAD_PROBE_TYPE_BUFFER)) return GST_PAD_PROBE_OK;
    const GstClockTime t = bufTime(info);
    const qint64 first = sh->firstPts;
    if (GST_CLOCK_TIME_IS_VALID(t) && first >= 0 && qint64(t) - first > sh->durationNs) return GST_PAD_PROBE_DROP;
    return GST_PAD_PROBE_OK;
}

static void toFakeSink(ClipExporter::Shared *sh, GstPad *pad)
{
    if (gst_pad_is_linked(pad)) return;
    GstElement *fs = make("fakesink");
    g_object_set(fs, "sync", FALSE, "async", FALSE, nullptr);
    gst_bin_add(GST_BIN(sh->pipe), fs);
    gst_element_sync_state_with_parent(fs);
    GstPad *p = gst_element_get_static_pad(fs, "sink");
    gst_pad_link(pad, p);
    gst_object_unref(p);
}

// Noi chuoi phan tu, roi noi pad nguon vao dau chuoi va cuoi chuoi vao mux
static bool linkChain(ClipExporter::Shared *sh, GstPad *src, std::vector<GstElement *> chain, const char *muxPad,
                      GstPad *existing = nullptr)
{
    for (GstElement *e : chain)
        if (!e) {
            for (GstElement *x : chain)
                if (x) gst_object_unref(x);
            return false;
        }
    for (GstElement *e : chain) gst_bin_add(GST_BIN(sh->pipe), e);
    for (size_t i = 0; i + 1 < chain.size(); ++i)
        if (!gst_element_link(chain[i], chain[i + 1])) return false;
    GstPad *mp = existing ? GST_PAD(gst_object_ref(existing)) : gst_element_request_pad_simple(sh->mux, muxPad);
    if (!mp) return false;
    GstPad *last = gst_element_get_static_pad(chain.back(), "src");
    const bool ok = gst_pad_link(last, mp) == GST_PAD_LINK_OK;
    gst_object_unref(last);
    gst_object_unref(mp);
    if (!ok) return false;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) gst_element_sync_state_with_parent(*it);
    GstPad *first = gst_element_get_static_pad(chain.front(), "sink");
    const bool ok2 = gst_pad_link(src, first) == GST_PAD_LINK_OK;
    gst_object_unref(first);
    return ok2;
}

static bool audioEncodingSupported(const gchar *enc)
{
    return enc && (g_ascii_strcasecmp(enc, "PCMA") == 0 || g_ascii_strcasecmp(enc, "PCMU") == 0 ||
                   g_ascii_strcasecmp(enc, "MPEG4-GENERIC") == 0);
}

// Goi cho tung luong trong SDP truoc khi SETUP: biet truoc co am thanh hay khong
static gboolean onSelectStream(GstElement *, guint, GstCaps *caps, gpointer ud)
{
    auto *sh = static_cast<ClipExporter::Shared *>(ud);
    if (caps && gst_caps_get_size(caps) > 0) {
        GstStructure *st = gst_caps_get_structure(caps, 0);
        const gchar *media = gst_structure_get_string(st, "media");
        const gchar *enc = gst_structure_get_string(st, "encoding-name");
        std::lock_guard<std::mutex> lk(sh->m);
        if (media && g_str_equal(media, "audio") && audioEncodingSupported(enc) && sh->sdpAudio.isEmpty())
            sh->sdpAudio = QString::fromUtf8(enc);
    }
    return TRUE;
}

static void onPadAdded(GstElement *, GstPad *pad, gpointer ud)
{
    auto *sh = static_cast<ClipExporter::Shared *>(ud);
    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (!caps) caps = gst_pad_query_caps(pad, nullptr);
    const gchar *media = nullptr, *enc = nullptr;
    if (caps && gst_caps_get_size(caps) > 0) {
        GstStructure *st = gst_caps_get_structure(caps, 0);
        media = gst_structure_get_string(st, "media");
        enc = gst_structure_get_string(st, "encoding-name");
    }
    bool used = false;
    std::lock_guard<std::mutex> lk(sh->m);
    if (media && g_str_equal(media, "video") && !sh->hasVideo && enc) {
        const bool h265 = g_ascii_strcasecmp(enc, "H265") == 0;
        const bool h264 = g_ascii_strcasecmp(enc, "H264") == 0;
        if (h264 || h265) {
            // xin pad am thanh truoc khi video bat dau chay
            if (sh->wantAudio && !sh->sdpAudio.isEmpty() && !sh->audioMuxPad && !sh->audioAdded)
                sh->audioMuxPad = gst_element_request_pad_simple(sh->mux, "audio_%u");
            GstElement *q = make("queue");
            GstElement *parse = make(h265 ? "h265parse" : "h264parse");
            std::vector<GstElement *> chain{make(h265 ? "rtph265depay" : "rtph264depay"), parse, q};
            if (linkChain(sh, pad, chain, "video_%u")) {
                sh->hasVideo = true;
                sh->videoCodec = h265 ? "H.265" : "H.264";
                GstPad *ps = gst_element_get_static_pad(parse, "src");
                gst_pad_add_probe(ps, GST_PAD_PROBE_TYPE_BUFFER, trackPts, sh, nullptr);
                gst_object_unref(ps);
                sh->queueSinks.push_back(gst_element_get_static_pad(q, "sink"));
                used = true;
            }
        }
    } else if (media && g_str_equal(media, "audio") && sh->wantAudio && !sh->audioAdded && audioEncodingSupported(enc)) {
        std::vector<GstElement *> chain;
        if (g_ascii_strcasecmp(enc, "PCMA") == 0)
            chain = {make("rtppcmadepay"), make("alawdec"), make("audioconvert"), make("audioresample"), make("voaacenc"), make("queue")};
        else if (g_ascii_strcasecmp(enc, "PCMU") == 0)
            chain = {make("rtppcmudepay"), make("mulawdec"), make("audioconvert"), make("audioresample"), make("voaacenc"), make("queue")};
        else
            chain = {make("rtpmp4gdepay"), make("aacparse"), make("queue")};
        GstElement *first = chain.front();
        if (linkChain(sh, pad, chain, "audio_%u", sh->audioMuxPad)) {
            sh->audioAdded = true;
            GstPad *ap = gst_element_get_static_pad(first, "src");
            gst_pad_add_probe(ap, GST_PAD_PROBE_TYPE_BUFFER, trimAudio, sh, nullptr);
            gst_object_unref(ap);
            sh->queueSinks.push_back(gst_element_get_static_pad(chain.back(), "sink"));
            used = true;
        }
    }
    if (!used) toFakeSink(sh, pad);
    if (caps) gst_caps_unref(caps);
}

ClipExporter::ClipExporter(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(250);
    connect(&m_timer, &QTimer::timeout, this, &ClipExporter::poll);
}

ClipExporter::~ClipExporter()
{
    if (m_pipe) {
        gst_element_set_state(m_pipe, GST_STATE_NULL);
        if (m_bus) gst_object_unref(m_bus);
        gst_object_unref(m_pipe);
    }
    if (m_sh) {
        for (GstPad *p : m_sh->queueSinks) gst_object_unref(p);
        if (m_sh->audioMuxPad) gst_object_unref(m_sh->audioMuxPad);
        delete m_sh;
    }
}

bool ClipExporter::start(const QString &rtspUrl, const QString &user, const QString &password, qint64 durationMs,
                         const QString &outFile, bool audio)
{
    if (m_pipe) return false;
    m_sh = new Shared;
    m_sh->wantAudio = audio;
    m_sh->durationNs = durationMs * 1000000LL;
    m_pipe = gst_pipeline_new("export");
    m_sh->pipe = m_pipe;
    GstElement *src = make("rtspsrc");
    m_sh->mux = make("mp4mux");
    GstElement *sink = make("filesink");
    if (!src || !m_sh->mux || !sink) {
        finish(false, QStringLiteral("Thiếu plugin GStreamer (rtspsrc/mp4mux)"));
        return false;
    }
    g_object_set(src, "location", rtspUrl.toUtf8().constData(), "protocols", 4 /*TCP*/, "latency", 1000,
                 "user-id", user.toUtf8().constData(), "user-pw", password.toUtf8().constData(), nullptr);
    g_object_set(sink, "location", outFile.toUtf8().constData(), nullptr);
    gst_bin_add_many(GST_BIN(m_pipe), src, m_sh->mux, sink, nullptr);
    gst_element_link(m_sh->mux, sink);
    g_signal_connect(src, "pad-added", G_CALLBACK(onPadAdded), m_sh);
    g_signal_connect(src, "select-stream", G_CALLBACK(onSelectStream), m_sh);
    m_bus = gst_element_get_bus(m_pipe);
    m_out = outFile;
    m_duration = durationMs;
    m_eosSent = false;
    m_videoClock.invalidate();
    m_lastDone = -1;
    m_stallClock.start();
    if (gst_element_set_state(m_pipe, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        finish(false, QStringLiteral("Không khởi động được pipeline"));
        return false;
    }
    m_timer.start();
    return true;
}

void ClipExporter::sendEos()
{
    if (m_eosSent || !m_sh) return;
    m_eosSent = true;
    m_eosClock.start();
    std::lock_guard<std::mutex> lk(m_sh->m);
    if (m_sh->queueSinks.empty()) {
        gst_element_send_event(m_pipe, gst_event_new_eos());
        return;
    }
    for (GstPad *p : m_sh->queueSinks) gst_pad_send_event(p, gst_event_new_eos());
}

void ClipExporter::stopAndSave()
{
    if (m_pipe) sendEos();
}

void ClipExporter::poll()
{
    if (!m_pipe) return;
    const qint64 first = m_sh->firstPts, last = m_sh->lastPts;
    const qint64 done = (first >= 0 && last >= first) ? (last - first) / 1000000 : 0;
    emit progress(qMin(done, m_duration), m_duration, QFileInfo(m_out).size());
    if (done != m_lastDone) {
        m_lastDone = done;
        m_stallClock.restart();
    }
    if (!m_eosSent && (m_sh->reachedEnd || done >= m_duration)) sendEos();
    // Da xin pad am thanh nhung camera khong gui tieng -> tra pad lai de mp4mux khong cho mai
    if (m_sh->hasVideo && !m_videoClock.isValid()) m_videoClock.start();
    if (m_videoClock.isValid() && m_videoClock.elapsed() > 4000) {
        std::lock_guard<std::mutex> lk(m_sh->m);
        if (m_sh->audioMuxPad && !m_sh->audioAdded) {
            gst_element_release_request_pad(m_sh->mux, m_sh->audioMuxPad);
            gst_object_unref(m_sh->audioMuxPad);
            m_sh->audioMuxPad = nullptr;
        }
    }

    while (GstMessage *msg = gst_bus_pop(m_bus)) {
        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS: {
            gst_message_unref(msg);
            const bool hasData = m_sh->hasVideo && done > 0;
            const QString what = m_sh->audioAdded ? QStringLiteral("%1 + AAC").arg(m_sh->videoCodec) : m_sh->videoCodec;
            finish(hasData, hasData ? QStringLiteral("Đã lưu %1 giây (%2)").arg(qRound(done / 1000.0)).arg(what)
                                    : QStringLiteral("Đầu ghi không trả dữ liệu cho khoảng thời gian này"));
            return;
        }
        case GST_MESSAGE_ERROR: {
            GError *e = nullptr;
            gst_message_parse_error(msg, &e, nullptr);
            const QString text = QString::fromUtf8(e ? e->message : "lỗi");
            if (e) g_error_free(e);
            gst_message_unref(msg);
            // Sau khi da gui EOS, rtspsrc co the bao loi "not-linked/eos" -> bo qua, cho mux ket thuc
            if (m_eosSent) continue;
            finish(false, QStringLiteral("Lỗi: %1").arg(text));
            return;
        }
        default:
            gst_message_unref(msg);
        }
    }
    if (m_eosSent && m_eosClock.elapsed() > 10000)
        return finish(false, QStringLiteral("Hết thời gian chờ ghi file (file có thể bị hỏng)"));
    if (!m_eosSent && m_stallClock.elapsed() > 20000) {
        if (done > 0) sendEos(); // dau ghi dung gui -> luu phan da co
        else finish(false, QStringLiteral("Đầu ghi không gửi dữ liệu (20 giây)"));
    }
}

void ClipExporter::finish(bool ok, const QString &msg)
{
    m_timer.stop();
    if (m_pipe) {
        gst_element_set_state(m_pipe, GST_STATE_NULL);
        if (m_bus) gst_object_unref(m_bus);
        gst_object_unref(m_pipe);
        m_pipe = nullptr;
        m_bus = nullptr;
    }
    if (m_sh) {
        for (GstPad *p : m_sh->queueSinks) gst_object_unref(p);
        if (m_sh->audioMuxPad) gst_object_unref(m_sh->audioMuxPad);
        delete m_sh;
        m_sh = nullptr;
    }
    if (!ok && QFileInfo(m_out).exists() && QFileInfo(m_out).size() < 1024) QFile::remove(m_out);
    if (qEnvironmentVariableIsSet("LITEPSS_HTTP_LOG")) {
        QFile f(AppInfo::dataFile("export.log"));
        if (f.open(QIODevice::Append | QIODevice::Text))
            f.write(QStringLiteral("%1 ok=%2 %3 -> %4\n").arg(QDateTime::currentDateTime().toString(Qt::ISODate))
                        .arg(ok).arg(msg, m_out).toUtf8());
    }
    emit finished(ok, msg);
}
