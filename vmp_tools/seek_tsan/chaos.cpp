#include <QGuiApplication>
#include <QTimer>
#include <QRandomGenerator>
#include <cstdio>
#include <gst/gst.h>
#include "videoPlayer.hpp"
// 25 s of random user actions while playing: seeks, jumps, steps, rate.
int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);
    QGuiApplication app(argc, argv);
    VideoPlayer pl; int actions = 0;
    pl.open(QUrl::fromLocalFile(argv[1]));
    QTimer t; t.setInterval(30);
    QObject::connect(&t, &QTimer::timeout, [&]() {
        quint32 r = QRandomGenerator::global()->bounded(100); ++actions;
        if (r < 30) pl.seek(QRandomGenerator::global()->bounded(138000), true);
        else if (r < 50) pl.seek(QRandomGenerator::global()->bounded(138000), false);
        else if (r < 60) pl.seekRelative(r % 2 ? 5000 : -5000);
        else if (r < 70) pl.stepForward();
        else if (r < 78) pl.stepBackward();
        else if (r < 85) pl.setRate(QList<double>{0.5, 1.0, 2.0, 4.0}[r % 4]);
        else pl.togglePlay();
    });
    QTimer::singleShot(500, [&]() { pl.play(); t.start(); });
    QTimer::singleShot(25000, [&]() { t.stop(); pl.seek(60000, true); pl.setRate(1.0); });
    QTimer::singleShot(32000, [&]() { printf("%d random actions, final id=%lld time=%lld\n", actions, pl.frameId(), pl.frameTime()); fflush(stdout); app.quit(); });
    return app.exec();
}
