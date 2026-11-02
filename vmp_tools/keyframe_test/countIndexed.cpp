// Counts the buffers that reach the decoder for every accurate seek.
//
// This is the measuring tool of the keyframe index: without the index a
// container without a keyframe table feeds hundreds of pictures to the
// decoder, with the index only the pictures of the group of pictures
// that the seek asked for.
//
// Not part of the project.
//
// Build:
//   qmake && make
// Run:
//   ./countIndexed <video>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTextStream>
#include <QThread>
#include <QVector>

#include <cstdio>

#include <gst/gst.h>

#include "keyframeIndex.hpp"
#include "videoPlayer.hpp"

int main( int argc, char *argv[] )
{
    gst_init( &argc, &argv );
    QCoreApplication app( argc, argv );

    if ( argc < 2 )
    {
        printf( "usage: countIndexed <video>\n" );
        return 1;
    }

    QString path = QString::fromLocal8Bit( argv[1] );

    // Build the index first and wait for it, so the seeks below are all
    // served from a complete table.
    KeyframeIndex index;
    QElapsedTimer clock;
    clock.start();
    index.start( path );
    while ( index.isScanning() )
    {
        app.processEvents( QEventLoop::AllEvents, 50 );
        QThread::msleep( 20 );
    }
    printf( "index: %d keyframes in %lld ms, error '%s'\n",
            index.keyframeCount(), ( long long ) clock.elapsed(),
            qPrintable( index.errorString() ) );
    fflush( stdout );

    VideoPlayer player;
    player.setKeyframeIndex( &index );
    player.open( QUrl::fromLocalFile( path ) );

    // Let the pipeline preroll.
    QThread::msleep( 1500 );

    QList<qint64> targets;
    targets << 5000 << 60000 << 1000 << 138300 << 70010
            << 33000 << 100000 << 125;

    for ( int useIndex = 0; useIndex < 2; ++useIndex )
    {
        player.setUseKeyframeIndex( useIndex == 1 );
        qint64 total = 0;
        int count = 0;
        int wrong = 0;
        printf( "\n%s:\n", useIndex == 1 ? "with index" : "without index" );
        fflush( stdout );
        for ( qint64 target : targets )
        {
            quint64 before = player.decoderBuffers();
            quint64 framesBefore = player.deliveredFrames();
            player.seek( target, true );

            // Wait for the picture of the seek.
            QElapsedTimer wait;
            wait.start();
            while ( player.deliveredFrames() == framesBefore
                    && wait.elapsed() < 8000 )
            {
                app.processEvents( QEventLoop::AllEvents, 20 );
                QThread::msleep( 10 );
            }
            qint64 buffers = static_cast<qint64>(
                player.decoderBuffers() - before );
            total += buffers;
            ++count;
            if ( player.frameId() < 0 )
            {
                ++wrong;
            }
            printf( "  target %7lld ms -> decoder buffers %4lld, "
                    "id %6lld, time %7lld ms\n",
                    ( long long ) target, ( long long ) buffers,
                    ( long long ) player.frameId(),
                    ( long long ) player.frameTime() );
            fflush( stdout );
        }
        printf( "  average %lld buffers per accurate seek, %d frames "
                "without a readable bar\n",
                count > 0 ? ( long long ) ( total / count ) : 0, wrong );
        fflush( stdout );
    }

    return 0;
}
