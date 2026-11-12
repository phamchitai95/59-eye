/* Dau ghi gia lap de thu LitePSS: RTSP H.265 Main 4:2:0 tai
 *   rtsp://127.0.0.1:8554/cam/realmonitor?channel=N&subtype=0|1
 *   rtsp://127.0.0.1:8554/cam/playback?...
 * RTSP server bo qua phan query nen moi kenh/subtype deu nhan cung mot luong (chia se, 1 lan ma hoa).
 * Tham so: [rong] [cao], mac dinh 704 576.
 */
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);
    const char *w = argc > 1 ? argv[1] : "704";
    const char *h = argc > 2 ? argv[2] : "576";
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    GstRTSPServer *server = gst_rtsp_server_new();
    gst_rtsp_server_set_address(server, "127.0.0.1");
    gst_rtsp_server_set_service(server, "8554");
    GstRTSPMountPoints *mounts = gst_rtsp_server_get_mount_points(server);

    gchar *launch = g_strdup_printf(
        "( videotestsrc is-live=true pattern=ball ! "
        "video/x-raw,format=I420,width=%s,height=%s,framerate=25/1 ! "
        "clockoverlay halignment=right valignment=top font-desc=\"Sans 24\" ! "
        "x265enc speed-preset=ultrafast tune=zerolatency key-int-max=50 bitrate=512 ! "
        "video/x-h265,profile=main ! rtph265pay name=pay0 pt=96 config-interval=1 )", w, h);

    const char *paths[] = {"/cam/realmonitor", "/cam/playback"};
    for (int i = 0; i < 2; ++i) {
        GstRTSPMediaFactory *f = gst_rtsp_media_factory_new();
        gst_rtsp_media_factory_set_launch(f, launch);
        gst_rtsp_media_factory_set_shared(f, TRUE);
        gst_rtsp_mount_points_add_factory(mounts, paths[i], f);
    }
    g_object_unref(mounts);
    g_free(launch);
    gst_rtsp_server_attach(server, NULL);
    g_print("Fake NVR RTSP: rtsp://127.0.0.1:8554/cam/realmonitor (%sx%s H.265 Main)\n", w, h);
    g_main_loop_run(loop);
    return 0;
}
