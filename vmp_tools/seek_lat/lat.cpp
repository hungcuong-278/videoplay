#include <QGuiApplication>
#include <QTimer>
#include <QElapsedTimer>
#include <cstdio>
#include <gst/gst.h>
#include "videoPlayer.hpp"
// For each target: issue one seek, wait for the first frame (max 6 s).
int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);
    QGuiApplication app(argc, argv);
    VideoPlayer pl; bool accurate = QString(argv[2]) == "acc";
    QList<qint64> targets = argc > 4 ? (QString(argv[4]) == "short" ? (argc > 5 ? QList<qint64>{atoll(argv[5])} : QList<qint64>{5000, 21000, 12000, 28000, 2500, 17000}) : QList<qint64>{600000, 3300000, 1500000, 120000, 2700000, 45000, 3590000, 1800000}) : QList<qint64>{5000, 60000, 1000, 138300, 70010, 33000, 100000, 125};
    int i = -1; QElapsedTimer clk; bool waiting = false; int bad = 0; qint64 total = 0, worst = 0;
    std::function<void()> next = [&]() {
        if (++i >= targets.size()) { printf("  %-5s avg %lld ms, worst %lld ms, unreadable frames after seek: %d\n", argv[2], total / targets.size(), worst, bad); fflush(stdout); app.quit(); return; }
        waiting = true; clk.start(); pl.seek(targets[i], accurate);
        QTimer::singleShot(6000, [&, n = i]() { if (waiting && i == n) { printf("  target %lld: no frame in 6 s\n", targets[n]); waiting = false; next(); } });
    };
    QObject::connect(&pl, &VideoPlayer::frameReady, [&]() {
        if (!waiting || pl.frameTime() < 0) return;
        waiting = false; qint64 ms = clk.elapsed(); total += ms; worst = qMax(worst, ms);
        if (pl.frameId() < 0) ++bad;
        if (argc > 3) printf("    target %6lld -> time %6lld id %5lld  (%lld ms)\n", targets[i], pl.frameTime(), pl.frameId(), ms);
        QTimer::singleShot(300, next); });
    pl.open(QUrl::fromLocalFile(argv[1]));
    QTimer::singleShot(800, next);
    return app.exec();
}
