#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>
#include <cstdio>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include "barcodeReader.hpp"
// Decode every frame as fast as possible and read the bar of each one.
int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);
    QString uri = gst_filename_to_uri(argv[1], nullptr);
    QString scale = argc > 2 ? argv[2] : "";
    QString desc = QString("uridecodebin uri=%1 ! videoconvert ! %2 appsink name=s sync=false "
                           "caps=video/x-raw,format=RGBA").arg(uri, scale.isEmpty() ? "" : "videoscale ! " + scale + " ! ");
    GstElement *p = gst_parse_launch(desc.toUtf8().constData(), nullptr);
    GstElement *s = gst_bin_get_by_name(GST_BIN(p), "s");
    gst_element_set_state(p, GST_STATE_PLAYING);
    QVector<qint64> ids; qint64 ns = 0; QElapsedTimer t; int w = 0, h = 0;
    while (GstSample *smp = gst_app_sink_pull_sample(GST_APP_SINK(s))) {
        GstVideoInfo info; gst_video_info_from_caps(&info, gst_sample_get_caps(smp));
        GstVideoFrame f; gst_video_frame_map(&f, &info, gst_sample_get_buffer(smp), GST_MAP_READ);
        QImage img((const uchar*)GST_VIDEO_FRAME_PLANE_DATA(&f,0), info.width, info.height,
                   GST_VIDEO_FRAME_PLANE_STRIDE(&f,0), QImage::Format_RGBA8888);
        w = info.width; h = info.height;
        qint64 id = -1; t.start(); if (!BarcodeReader::read(img, id)) id = -1; ns += t.nsecsElapsed();
        ids.append(id); gst_video_frame_unmap(&f); gst_sample_unref(smp);
    }
    gst_element_set_state(p, GST_STATE_NULL);
    int fail = 0; for (qint64 v : ids) if (v < 0) ++fail;
    int mismatch = -1;
    if (argc > 3) { mismatch = 0; QFile fl(argv[3]); fl.open(QIODevice::ReadOnly); QTextStream ts(&fl);
        int i = 0; while (!ts.atEnd()) { qint64 v = ts.readLine().toLongLong();
            if (i >= ids.size() || ids[i] != v) ++mismatch; ++i; }
        if (i != ids.size()) printf("  frame count differs: py %d cpp %d\n", i, ids.size()); }
    printf("%-22s %4dx%-4d frames=%d unreadable=%d mismatch_vs_python=%d avg=%.1f us/frame first=%lld last=%lld\n",
           qPrintable(QString(argv[1]).section('/',-1)), w, h, ids.size(), fail, mismatch,
           ids.isEmpty() ? 0.0 : ns / 1000.0 / ids.size(), ids.isEmpty() ? -1 : ids.first(), ids.isEmpty() ? -1 : ids.last());
    return 0;
}
