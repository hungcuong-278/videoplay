#include <QGuiApplication>
#include <QTimer>
#include <QElapsedTimer>
#include <cstdio>
#include <gst/gst.h>
#include "videoPlayer.hpp"
// Simulated drag: the handle moves across the file in 4 s (a move every
// 40 ms, keyframe seeks), then is released (accurate seek).
int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);
    QGuiApplication app(argc, argv);
    VideoPlayer pl; QElapsedTimer clk; int moves = 0, shown = 0, wrong = 0; qint64 lastFrameAt = 0, maxGap = 0;
    qint64 from = atoll(argv[2]), to = atoll(argv[3]); bool dragging = false, released = false;
    QObject::connect(&pl, &VideoPlayer::frameReady, [&]() {
        if (!dragging && !released) return;
        if (pl.frameTime() < 0) return;
        ++shown; if (pl.frameId() < 0) ++wrong;
        qint64 now = clk.elapsed(); if (dragging) maxGap = qMax(maxGap, now - lastFrameAt); lastFrameAt = now;
        if (released) { printf("  released at %lld -> exact frame time=%lld id=%lld after %lld ms\n", to, pl.frameTime(), pl.frameId(), now - 4000); fflush(stdout); app.quit(); }
    });
    QTimer move; move.setInterval(40);
    QObject::connect(&move, &QTimer::timeout, [&]() {
        qint64 t = from + (to - from) * clk.elapsed() / 4000;
        if (clk.elapsed() >= 4000) { move.stop(); dragging = false;
            printf("  drag: %d handle moves -> %d pictures shown (%.1f per s), longest wait %lld ms, unreadable %d\n", moves, shown, shown / 4.0, maxGap, wrong);
            released = true; pl.seek(to, true); return; }
        ++moves; pl.seek(t, false);
    });
    pl.open(QUrl::fromLocalFile(argv[1]));
    QTimer::singleShot(1500, [&]() { dragging = true; clk.start(); move.start(); });
    QTimer::singleShot(30000, [&]() { printf("  timeout\n"); app.quit(); });
    return app.exec();
}
