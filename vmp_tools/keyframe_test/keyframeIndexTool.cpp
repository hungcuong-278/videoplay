// Verification tool for KeyframeIndex.
//
// Builds the index of a video file and prints what it found, so the
// result can be compared with the keyframes ffprobe reports. Not part of
// the project.
//
// Build:
//   qmake && make
// Run:
//   ./keyframeIndexTool <video> [expected count]

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTextStream>
#include <QThread>
#include <QVector>

#include <cstdio>

#include <gst/gst.h>

#include "keyframeIndex.hpp"

int main( int argc, char *argv[] )
{
    gst_init( &argc, &argv );
    QCoreApplication app( argc, argv );

    if ( argc < 2 )
    {
        printf( "usage: keyframeIndexTool <video> [expected count]\n" );
        return 1;
    }

    QTextStream out( stdout );
    KeyframeIndex index;
    QElapsedTimer clock;
    clock.start();
    index.start( QString::fromLocal8Bit( argv[1] ) );

    // Wait for the scan to finish
    while ( index.isScanning() )
    {
        app.processEvents( QEventLoop::AllEvents, 50 );
        QThread::msleep( 20 );
    }
    qint64 elapsed = clock.elapsed();

    QVector<qint64> times = index.keyframeTimes();
    out << "file: " << argv[1] << "\n";
    out << "keyframes: " << times.size() << "\n";
    out << "scan time: " << elapsed << " ms\n";
    out << "exact duration: " << index.durationMs() << " ms\n";
    out << "largest gap: " << index.largestGapMs() << " ms\n";
    out << "error: " << index.errorString() << "\n";
    for ( int i = 0; i < times.size(); ++i )
    {
        out << "  " << i << ": " << times[i] << " ms\n";
    }

    if ( argc > 2 )
    {
        int expected = QString( argv[2] ).toInt();
        out << "expected: " << expected << "\n";
        out << ( times.size() == expected ? "MATCH\n" : "DIFFERENT\n" );
    }
    out.flush();
    return 0;
}
