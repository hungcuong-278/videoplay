#include "videoCutter.hpp"

#include <QDebug>
#include <QFileInfo>
#include <QMetaObject>
#include <QThread>

#include <QtConcurrent/QtConcurrentRun>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

namespace
{
// How long the cutter waits before it looks at the bus again
const int POLL_INTERVAL_MS = 30;

/*!
 * \brief Keeps the state of the barcode reading.
 */
struct IdState
{
    VideoCutter::CancelFlag cancel;
    QMutex mutex;
    QStringList ids;
    int count = 0;
};

/*!
 * \brief Links the picture pad of the demuxer with the converter.
 *
 * The demuxer only knows the picture stream when it has read a little of
 * the file, so the chain is built here. The audio stream is ignored.
 */
void onDecoderPadAdded( GstElement *_element, GstPad *_pad, gpointer _data )
{
    GstElement *pipeline = static_cast<GstElement *>( _data );

    // Only the picture stream is of interest
    GstCaps *caps = gst_pad_get_current_caps( _pad );
    if ( caps != nullptr )
    {
        GstStructure *structure = gst_caps_get_structure( caps, 0 );
        const char *name = ( structure != nullptr )
                           ? gst_structure_get_name( structure ) : "";
        bool isVideo = g_str_has_prefix( name, "video/" );
        gst_caps_unref( caps );
        if ( !isVideo )
        {
            return;
        }
    }

    GstElement *convert = gst_bin_get_by_name( GST_BIN( pipeline ), "convert" );
    if ( convert == nullptr )
    {
        return;
    }
    GstPad *sinkPad = gst_element_get_static_pad( convert, "sink" );
    if ( sinkPad != nullptr )
    {
        if ( gst_pad_link( _pad, sinkPad ) != GST_PAD_LINK_OK )
        {
            qWarning() << "Cannot link the demuxer with the converter";
        }
        gst_object_unref( sinkPad );
    }
    gst_object_unref( convert );
}

/*!
 * \brief Reads the barcode of one picture of the new file.
 */
GstFlowReturn onVerifyFrame( GstAppSink *_sink, GstBuffer *_buffer,
                             gpointer _data )
{
    Q_UNUSED( _sink );
    IdState *state = static_cast<IdState *>( _data );
    if ( state->cancel->load() )
    {
        return GST_FLOW_EOS;
    }

    GstMapInfo map;
    if ( !gst_buffer_map( _buffer, &map, GST_MAP_READ ) )
    {
        return GST_FLOW_OK;
    }
    // The picture is in RGBA, because the pipeline asks for that format.
    QImage frame( map.data, map.size, map.info.width, map.info.height,
                  map.info.width * 4, QImage::Format_RGBA8888 );
    qint64 frameId = -1;
    if ( BarcodeReader::read( frame, frameId ) )
    {
        QMutexLocker locker( &state->mutex );
        state->ids.append( QString::number( frameId ) );
    }
    gst_buffer_unmap( _buffer, &map );

    {
        QMutexLocker locker( &state->mutex );
        ++state->count;
    }
    return GST_FLOW_OK;
}
}

VideoCutter::VideoCutter( QObject *_parent )
    : QObject( _parent ),
      m_running( false ),
      m_progress( 0 ),
      m_framesVerified( 0 ),
      m_cutStartMs( 0 ),
      m_cutEndMs( 0 )
{
    connect( &m_watcher, &QFutureWatcher<CutResult>::finished,
             this, &VideoCutter::onCutFinished );
}

VideoCutter::~VideoCutter()
{
    if ( m_cancel )
    {
        m_cancel->store( true );
    }
    m_watcher.waitForFinished();
}

void VideoCutter::start( const QString &_videoPath, const QString &_targetPath,
                         qint64 _startMs, qint64 _endMs,
                         qint64 _startId, qint64 _endId )
{
    if ( m_running )
    {
        setError( "A cut is already running" );
        return;
    }
    if ( !QFileInfo::exists( _videoPath ) )
    {
        setError( "File not found: " + _videoPath );
        return;
    }
    if ( _endMs <= _startMs )
    {
        setError( "The end of the cut is before its start" );
        return;
    }

    m_cancel = std::make_shared<std::atomic<bool> >( false );
    m_progress = 0;
    m_framesVerified = 0;
    m_keptIds.clear();
    m_resultPath.clear();
    m_errorString.clear();
    m_cutStartMs = 0;
    m_cutEndMs = 0;
    setStatus( "Starting" );

    m_running = true;
    emit runningChanged();

    // The progress is reported from the pool thread and handed to the
    // GUI thread with a queued call.
    std::function< void( int, const QString & ) > progress =
        [this]( int _percent, const QString &_text )
    {
        QMetaObject::invokeMethod(
            this, [this, _percent, _text]()
            {
                if ( _percent != m_progress )
                {
                    m_progress = _percent;
                    emit progressChanged();
                }
                setStatus( _text );
            }, Qt::QueuedConnection );
    };

    m_watcher.setFuture( QtConcurrent::run(
        [this, _videoPath, _targetPath, _startMs, _endMs, _startId, _endId,
         progress]() { return runCut( _videoPath, _targetPath, _startMs,
                                     _endMs, _startId, _endId, m_cancel,
                                     progress ); } ) );
}

bool VideoCutter::isRunning() const
{
    return m_running;
}

int VideoCutter::progress() const
{
    return m_progress;
}

QString VideoCutter::statusText() const
{
    return m_statusText;
}

QString VideoCutter::resultPath() const
{
    return m_resultPath;
}

QString VideoCutter::errorString() const
{
    return m_errorString;
}

int VideoCutter::framesVerified() const
{
    return m_framesVerified;
}

QStringList VideoCutter::keptIds() const
{
    return m_keptIds;
}

qint64 VideoCutter::cutStartMs() const
{
    return m_cutStartMs;
}

qint64 VideoCutter::cutEndMs() const
{
    return m_cutEndMs;
}

void VideoCutter::setStatus( const QString &_text )
{
    if ( _text == m_statusText )
    {
        return;
    }
    m_statusText = _text;
    emit statusChanged();
}

void VideoCutter::setError( const QString &_text )
{
    m_errorString = _text;
    emit failed();
}

void VideoCutter::onCutFinished()
{
    CutResult result = m_watcher.result();
    m_running = false;
    emit runningChanged();

    if ( !result.ok )
    {
        m_errorString = result.errorString;
        emit failed();
        return;
    }
    m_resultPath = result.resultPath;
    m_keptIds = result.keptIds;
    m_framesVerified = result.framesVerified;
    m_cutStartMs = result.cutStartMs;
    m_cutEndMs = result.cutEndMs;
    m_progress = 100;
    emit progressChanged();
    emit finished();
}

VideoCutter::CutResult VideoCutter::runCut(
    const QString &_videoPath, const QString &_targetPath,
    qint64 _startMs, qint64 _endMs, qint64 _startId, qint64 _endId,
    const CancelFlag &_cancel,
    const std::function< void( int, const QString & ) > &_progress )
{
    CutResult result;

    // A file that is already there is not overwritten, so the name gets
    // a number.
    QString targetPath = _targetPath;
    if ( !targetPath.endsWith( ".mp4" ) )
    {
        targetPath += ".mp4";
    }
    int number = 1;
    while ( QFileInfo::exists( targetPath ) )
    {
        QString base = _targetPath;
        if ( base.endsWith( ".mp4" ) )
        {
            base = base.left( base.size() - 4 );
        }
        targetPath = QString( "%1_cut%2.mp4" ).arg( base ).arg( number );
        ++number;
    }
    result.resultPath = targetPath;

    // Step 1: copy the stream into the new file. The pictures are not
    // changed, so the barcode of a picture stays readable.
    if ( _progress )
    {
        _progress( 0, "Copying the stream" );
    }
    qint64 cutStartMs = 0;
    qint64 cutEndMs = 0;
    QString errorString;
    if ( !copyStream( _videoPath, targetPath, _startMs, _endMs, _cancel,
                      _progress, cutStartMs, cutEndMs, errorString ) )
    {
        result.errorString = errorString;
        return result;
    }
    result.cutStartMs = cutStartMs;
    result.cutEndMs = cutEndMs;

    // Step 2: read the barcode of every picture of the new file. The
    // ids it finds are the real ids, so the rows of the metadata table
    // can be cut exactly.
    if ( _progress )
    {
        _progress( 60, "Reading the barcodes of the new file" );
    }
    int count = 0;
    std::function< void( int ) > verifyProgress =
        [&_progress]( int _done )
    {
        if ( _progress )
        {
            // The second half of the work is the reading of the barcodes
            _progress( 60 + 40 * _done / 100, "Reading the barcodes" );
        }
    };
    QStringList ids = readIds( targetPath, _cancel, verifyProgress, count );
    if ( _cancel->load() )
    {
        result.errorString = "Cut cancelled";
        return result;
    }

    result.keptIds = filterIds( ids, _startId, _endId );
    result.framesVerified = count;
    if ( _progress )
    {
        _progress( 100, "Done" );
    }
    result.ok = true;
    return result;
}

bool VideoCutter::copyStream( const QString &_videoPath,
                              const QString &_targetPath,
                              qint64 _startMs, qint64 _endMs,
                              const CancelFlag &_cancel,
                              const std::function< void( int,
                                        const QString & ) > &_progress,
                              qint64 &cutStartMs_, qint64 &cutEndMs_,
                              QString &_errorString )
{
    GError *error = nullptr;
    gchar *sourceUri = gst_filename_to_uri( _videoPath.toUtf8().constData(),
                                             &error );
    if ( sourceUri == nullptr )
    {
        _errorString = QString::fromUtf8( error->message );
        g_error_free( error );
        return false;
    }
    gchar *targetUri = gst_filename_to_uri( _targetPath.toUtf8().constData(),
                                             &error );
    if ( targetUri == nullptr )
    {
        _errorString = QString::fromUtf8( error->message );
        g_error_free( error );
        g_free( sourceUri );
        return false;
    }

    // The stream is copied, never encoded:
    // filesrc ! tsdemux ! h264parse ! mpegtsmux ! filesink
    // The parser and the muxer only repackage the data, so every
    // picture of the new file is the same picture as in the original.
    GstElement *pipeline = gst_pipeline_new( "videoCopy" );
    GstElement *source = gst_element_factory_make( "filesrc", "source" );
    GstElement *demux = gst_element_factory_make( "tsdemux", "demux" );
    GstElement *parser = gst_element_factory_make( "h264parse", "parser" );
    GstElement *muxer = gst_element_factory_make( "mpegtsmux", "muxer" );
    GstElement *sink = gst_element_factory_make( "filesink", "sink" );
    if ( pipeline == nullptr || source == nullptr || demux == nullptr
         || parser == nullptr || muxer == nullptr || sink == nullptr )
    {
        _errorString = "Missing GStreamer elements";
        if ( pipeline != nullptr )
        {
            gst_object_unref( pipeline );
        }
        g_free( sourceUri );
        g_free( targetUri );
        return false;
    }

    g_object_set( source, "location", sourceUri, nullptr );
    g_object_set( sink, "location", targetUri, nullptr );
    g_free( sourceUri );
    g_free( targetUri );

    gst_bin_add_many( GST_BIN( pipeline ), source, demux, parser, muxer,
                      sink, nullptr );
    if ( !gst_element_link_many( source, demux, parser, muxer, sink,
                                 nullptr ) )
    {
        _errorString = "Cannot link the copy pipeline";
        gst_object_unref( pipeline );
        return false;
    }

    GstBus *bus = gst_pipeline_get_bus( GST_PIPELINE( pipeline ) );
    gst_element_set_state( pipeline, GST_STATE_PLAYING );

    // The seek makes the demuxer start at the keyframe before the
    // marked picture. The copy therefore holds a few pictures too many,
    // which the barcode reading afterwards finds out.
    gboolean seekOk = gst_element_seek(
        pipeline, 1.0, GST_FORMAT_TIME,
        GST_SEEK_FLAG_ACCURATE, GST_SEEK_TYPE_SET,
        _startMs * GST_MSECOND, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE );
    if ( !seekOk )
    {
        // A container that cannot be seeked is copied from the start.
        qWarning() << "Cannot seek to" << _startMs << "ms, copying all";
    }

    // The end of the cut is reached by the position of the pipeline.
    bool reachedEnd = false;
    bool failed = false;
    while ( true )
    {
        if ( _cancel->load() )
        {
            _errorString = "Cut cancelled";
            break;
        }

        GstMessage *message = gst_bus_timed_pop_filtered(
            bus, POLL_INTERVAL_MS * GST_MSECOND,
            GST_MESSAGE_EOS | GST_MESSAGE_ERROR );
        if ( message != nullptr )
        {
            if ( GST_MESSAGE_TYPE( message ) == GST_MESSAGE_ERROR )
            {
                GError *messageError = nullptr;
                gst_message_parse_error( message, &messageError, nullptr );
                _errorString = QString::fromUtf8( messageError->message );
                g_error_free( messageError );
                failed = true;
            }
            gst_message_unref( message );
            break;
        }

        gint64 position = 0;
        if ( gst_element_query_position( pipeline, GST_FORMAT_TIME,
                                         &position ) )
        {
            qint64 positionMs = static_cast<qint64>( position / GST_MSECOND );
            if ( _progress )
            {
                int percent = qBound( 0, static_cast<int>(
                    60 * ( positionMs - _startMs )
                        / qMax<qint64>( 1, _endMs - _startMs ) ), 60 );
                _progress( percent, "Copying the stream" );
            }
            if ( positionMs >= _endMs )
            {
                reachedEnd = true;
                break;
            }
        }
    }

    // The pipeline is stopped, so the file is closed properly and the
    // index of the new file is written.
    gst_element_set_state( pipeline, GST_STATE_NULL );
    gst_object_unref( bus );
    gst_object_unref( pipeline );

    if ( failed )
    {
        return false;
    }
    if ( !reachedEnd )
    {
        _errorString = "The end of the cut was not reached";
        return false;
    }
    cutStartMs_ = _startMs;
    cutEndMs_ = _endMs;
    return true;
}

QStringList VideoCutter::readIds( const QString &_path,
                                  const CancelFlag &_cancel,
                                  const std::function< void( int ) > &_progress,
                                  int &countOut_ )
{
    QStringList result;
    countOut_ = 0;

    GError *error = nullptr;
    gchar *fileUri = gst_filename_to_uri( _path.toUtf8().constData(),
                                          &error );
    if ( fileUri == nullptr )
    {
        g_error_free( error );
        return result;
    }

    // The file is decoded, but nothing is written, so the pictures are
    // the same as in the original.
    GstElement *pipeline = gst_pipeline_new( "idReader" );
    GstElement *demux = gst_element_factory_make( "uridecodebin", "decode" );
    GstElement *convert = gst_element_factory_make( "videoconvert", "convert" );
    GstElement *sink = gst_element_factory_make( "appsink", "sink" );
    if ( pipeline == nullptr || demux == nullptr || convert == nullptr
         || sink == nullptr )
    {
        g_free( fileUri );
        if ( pipeline != nullptr )
        {
            gst_object_unref( pipeline );
        }
        return result;
    }

    // The demuxer and the decoder are chosen by the element itself, so
    // every container and every codec works without a change.
    g_object_set( demux, "uri", fileUri, nullptr );
    g_free( fileUri );
    g_object_set( sink, "emit-signals", FALSE, nullptr );
    g_object_set( sink, "sync", FALSE, nullptr );
    g_object_set( sink, "max-buffers", 1, nullptr );
    g_object_set( sink, "drop", FALSE, nullptr );
    GstCaps *caps = gst_caps_from_string( "video/x-raw,format=RGBA" );
    g_object_set( sink, "caps", caps, nullptr );
    gst_caps_unref( caps );

    gst_bin_add_many( GST_BIN( pipeline ), demux, convert, sink, nullptr );
    // The demuxer has no picture pad before the stream has been read a
    // little, so the chain from it to the converter is built when its
    // pad appears.
    if ( !gst_element_link_many( convert, sink, nullptr ) )
    {
        gst_object_unref( pipeline );
        return result;
    }

    g_signal_connect( demux, "pad-added",
                      G_CALLBACK( &onDecoderPadAdded ), pipeline );

    IdState state;
    state.cancel = _cancel;
    g_signal_connect( sink, "new-sample",
                      G_CALLBACK( &onVerifyFrame ), &state );

    GstBus *bus = gst_pipeline_get_bus( GST_PIPELINE( pipeline ) );
    gst_element_set_state( pipeline, GST_STATE_PLAYING );

    int lastReported = 0;
    while ( true )
    {
        if ( _cancel->load() )
        {
            break;
        }
        GstMessage *message = gst_bus_timed_pop_filtered(
            bus, POLL_INTERVAL_MS * GST_MSECOND,
            GST_MESSAGE_EOS | GST_MESSAGE_ERROR );
        if ( message != nullptr )
        {
            if ( GST_MESSAGE_TYPE( message ) == GST_MESSAGE_ERROR )
            {
                GError *messageError = nullptr;
                gst_message_parse_error( message, &messageError, nullptr );
                g_error_free( messageError );
            }
            gst_message_unref( message );
            break;
        }
        int count = 0;
        {
            QMutexLocker locker( &state.mutex );
            count = state.count;
        }
        if ( count != lastReported )
        {
            lastReported = count;
            if ( _progress )
            {
                _progress( count );
            }
        }
    }

    gst_element_set_state( pipeline, GST_STATE_NULL );
    gst_object_unref( bus );
    gst_object_unref( pipeline );

    {
        QMutexLocker locker( &state.mutex );
        result = state.ids;
        countOut_ = state.count;
    }
    return result;
}

QStringList VideoCutter::filterIds( const QStringList &_ids,
                                     qint64 _startId, qint64 _endId )
{
    QStringList result;
    if ( _startId < 0 || _endId < 0 )
    {
        // Without a marked range every id of the new file belongs to the
        // cut.
        return _ids;
    }
    for ( const QString &id : _ids )
    {
        bool ok = false;
        qint64 value = id.toLongLong( &ok );
        if ( !ok )
        {
            continue;
        }
        if ( value >= _startId && value <= _endId )
        {
            result.append( id );
        }
    }
    return result;
}
