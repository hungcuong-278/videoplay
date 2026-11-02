#include <QGuiApplication>
#include <QTimer>
#include <QElapsedTimer>
#include <cstdio>
#include <sys/resource.h>
#include <gst/gst.h>
#include "videoPlayer.hpp"
// Play <seconds> from <startMs>; report CPU time used per wall second and
// the cost of one duration query.
static double cpuSeconds() { rusage u; getrusage(RUSAGE_SELF, &u);
    return u.ru_utime.tv_sec + u.ru_stime.tv_sec + (u.ru_utime.tv_usec + u.ru_stime.tv_usec) / 1e6; }
int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);
    QGuiApplication app(argc, argv);
    VideoPlayer pl; double c0 = 0; QElapsedTimer wall; qint64 secs = atoll(argv[3]);
    pl.open(QUrl::fromLocalFile(argv[1]));
    QTimer::singleShot(1000, [&]() { pl.seek(atoll(argv[2]), true); });
    QTimer::singleShot(6000, [&]() { pl.play(); c0 = cpuSeconds(); wall.start(); });
    QTimer::singleShot(6000 + secs * 1000, [&]() {
        double used = cpuSeconds() - c0;
        printf("  playing %lld s: CPU %.0f%% of one core, durations seen: last=%lld ms\n", secs, 100.0 * used / (wall.elapsed() / 1000.0), pl.duration());
        fflush(stdout); app.quit(); });
    return app.exec();
}
