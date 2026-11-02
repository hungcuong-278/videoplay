#include <QGuiApplication>
#include <QTimer>
#include <QElapsedTimer>
#include <functional>
#include <cstdio>
#include <gst/gst.h>
#include "videoPlayer.hpp"
int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);
    QGuiApplication app(argc, argv);
    VideoPlayer pl;
    QList<std::function<void()>> acts; QList<QString> names;
    auto add = [&](QString n, std::function<void()> f) { names << n; acts << f; };
    add("seek 138560 (last frame)", [&]() { pl.seek(138560, true); });
    for (int i = 0; i < 7; ++i) add("  step -1", [&]() { pl.stepBackward(); });
    for (int i = 0; i < 3; ++i) add("  step +1", [&]() { pl.stepForward(); });
    add("seek 5000 (frame cut by seek)", [&]() { pl.seek(5000, true); });
    for (int i = 0; i < 3; ++i) add("  step -1", [&]() { pl.stepBackward(); });
    add("seek 0", [&]() { pl.seek(0, true); });
    add("  step -1 at first frame", [&]() { pl.stepBackward(); });
    int i = -1; qint64 lastCount = 0; QElapsedTimer quiet, clk; quint64 frames = 0;
    QObject::connect(&pl, &VideoPlayer::frameReady, [&]() { ++frames; quiet.start(); });
    QTimer poll; poll.setInterval(50);
    QObject::connect(&poll, &QTimer::timeout, [&]() {
        // next action once no frame arrived for 2.5 s (step-back retries need several seeks)
        if (i >= 0 && quiet.elapsed() < 2500) return;
        if (i >= 0) { printf("%-32s -> id=%5lld time=%6lld  (%lld ms)\n", qPrintable(names[i]), pl.frameId(), pl.frameTime(), clk.elapsed() - 2500); fflush(stdout); }
        if (++i >= acts.size()) { app.quit(); return; }
        clk.start(); quiet.start(); acts[i]();
    });
    pl.open(QUrl::fromLocalFile(argv[1]));
    QTimer::singleShot(800, [&]() { poll.start(); });
    return app.exec();
}
