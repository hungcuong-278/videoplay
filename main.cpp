#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QFileInfo>
#include <QUrl>

#include <gst/gst.h>

#include "videoPlayer.hpp"
#include "frameView.hpp"
#include "metadataStore.hpp"
#include "keyframeIndex.hpp"
#include "videoCutter.hpp"
#include "cutController.hpp"
#include "panTiltGraph.hpp"

/*!
 * \brief Converts a command-line path to a local file URL.
 * \param _argument Path as typed by the user.
 */
static QUrl argumentToUrl( const char *_argument )
{
    return QUrl::fromLocalFile(
        QFileInfo( QString::fromLocal8Bit( _argument ) ).absoluteFilePath() );
}

int main( int argc, char *argv[] )
{
    gst_init( &argc, &argv );

    QCoreApplication::setAttribute( Qt::AA_EnableHighDpiScaling );
    QGuiApplication app( argc, argv );

    qmlRegisterType<FrameView>( "VideoTool", 1, 0, "FrameView" );
    qmlRegisterType<PanTiltGraph>( "VideoTool", 1, 0, "PanTiltGraph" );

    // Declared before the engine so they are destroyed after it: QML keeps
    // references to them until the engine is gone.
    VideoPlayer player;
    MetadataStore metadata;
    KeyframeIndex keyframes;
    VideoCutter cutter;
    CutController cut;

    // Both objects live in the GUI thread, so these are direct calls.
    QObject::connect( &player, &VideoPlayer::frameReady, &metadata,
                      [&player, &metadata]()
    {
        metadata.recordFrame( player.frameId() );
    } );
    QObject::connect( &player, &VideoPlayer::sourceChanged,
                      &metadata, &MetadataStore::resetStats );

    // The player uses the index to skip the pictures that an accurate
    // seek would throw away anyway.
    player.setKeyframeIndex( &keyframes );

    // The index gives the exact duration of the file, so the handle of
    // the slider does not drift on a file without a duration field.
    QObject::connect( &keyframes, &KeyframeIndex::keyframeChanged,
                      &player, [&player]() { player.refreshDuration(); } );

    // The cut controller marks the frames on screen and cuts the video
    // and the metadata table.
    cut.setPlayer( &player );
    cut.setMetadata( &metadata );

    // Every new file gets a new index. The scan runs in the background,
    // so the video can be opened and played right away.
    QObject::connect( &player, &VideoPlayer::sourceChanged, &keyframes,
                      [&player, &keyframes]()
    {
        QString path = player.source();
        if ( !path.startsWith( "file://" ) )
        {
            keyframes.stop();
            return;
        }
        keyframes.start( QUrl( path ).toLocalFile() );
    } );

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty( "videoPlayer", &player );
    engine.rootContext()->setContextProperty( "metadata", &metadata );
    engine.rootContext()->setContextProperty( "keyframes", &keyframes );
    engine.rootContext()->setContextProperty( "cutter", &cutter );
    engine.rootContext()->setContextProperty( "cut", &cut );
    engine.load( QUrl( QStringLiteral( "qrc:/qml/main.qml" ) ) );
    if ( engine.rootObjects().isEmpty() )
    {
        return -1;
    }

    // Optional: <video> [csv]. Without a CSV argument, a CSV with the same
    // base name next to the video is loaded automatically.
    if ( argc > 1 )
    {
        QUrl videoUrl = argumentToUrl( argv[1] );
        player.open( videoUrl );
        if ( argc > 2 )
        {
            metadata.load( argumentToUrl( argv[2] ) );
        }
        else
        {
            metadata.loadForVideo( videoUrl );
        }
    }

    return app.exec();
}
