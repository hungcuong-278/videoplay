#include "keyframeIndex.hpp"

#include <QDataStream>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QMutexLocker>
#include <QStandardPaths>

#include <cstring>

#include <gst/gst.h>

namespace
{
// Magic and version of the cache file
const char CACHE_MAGIC[8] = { 'V', 'M', 'P', 'I', 'D', 'X', '1', '\0' };
// Extension of the cache file written next to the video
const char CACHE_SUFFIX[] = ".vmpidx";
// Only the first bytes of a buffer are looked at: the NAL header of the
// entry point picture is always near the start of the access unit.
const int NAL_SCAN_LIMIT = 4096;
// How long the scanner waits before it looks at the bus again
const int POLL_INTERVAL_MS = 50;

/*!
 * \brief State of one running scan.
 *
 * The probe of the parser pad writes into it. Only the streaming thread
 * of the pipeline touches it, except for the cancel flag, which is
 * atomic and may be set from the GUI thread. The mutex guards the
 * results that the thread of the scan reads while the probe writes them.
 */
struct ScanState
{
    QSharedPointer<QAtomicInt> cancel;
    QMutex mutex;
    QVector<qint64> times;     // stream time of every keyframe, in ms
    QVector<qint64> pts;       // raw timestamp of every keyframe, in ns
    GstSegment segment;
    bool haveSegment = false;
    int codec = 0;
    int lengthSize = 0;
    int percent = 0;
    qint64 fileSize = 0;
    qint64 bytesRead = 0;
    // The time of the last picture of the file. Together with the time
    // of one picture it gives the exact duration.
    qint64 lastPictureMs = -1;
    qint64 lastPictureEndMs = -1;
    qint64 firstPictureMs = -1;
    bool haveVideoPad = false;
    bool failed = false;
    QString errorString;
};

/*!
 * \brief Reads the keyframes of one stream of the file.
 *
 * Runs on the streaming thread of the pipeline. The segment event of the
 * pad gives the same timeline the player uses, so the times of the index
 * and the times of the player mean the same thing.
 */
GstPadProbeReturn onScanBuffer( GstPad *_pad, GstPadProbeInfo *_info,
                                gpointer _data )
{
    ScanState *state = static_cast<ScanState *>( _data );
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER( _info );
    if ( buffer == nullptr )
    {
        return GST_PAD_PROBE_OK;
    }
    GstCaps *caps = gst_pad_get_current_caps( _pad );
    if ( caps == nullptr )
    {
        return GST_PAD_PROBE_OK;
    }
    int codec = NalParser::codecFromCaps( caps );
    if ( codec == NalParser::CODEC_OTHER )
    {
        // Not an H.264 or H.265 stream: nothing to look for.
        gst_caps_unref( caps );
        return GST_PAD_PROBE_DROP;
    }

    QMutexLocker locker( &state->mutex );
    state->codec = codec;
    state->lengthSize = NalParser::lengthSizeFromCaps( caps );
    state->haveVideoPad = true;

    // The time of this picture on the stream timeline, the same one the
    // player shows.
    qint64 timeMs = -1;
    qint64 endMs = -1;
    if ( GST_BUFFER_PTS_IS_VALID( buffer ) )
    {
        if ( state->haveSegment )
        {
            guint64 streamTime = gst_segment_to_stream_time(
                &state->segment, GST_FORMAT_TIME,
                GST_BUFFER_PTS( buffer ) );
            if ( GST_CLOCK_TIME_IS_VALID( streamTime ) )
            {
                timeMs = static_cast<qint64>( streamTime / GST_MSECOND );
            }
        }
        if ( GST_BUFFER_DURATION_IS_VALID( buffer ) )
        {
            endMs = timeMs + static_cast<qint64>(
                GST_BUFFER_DURATION( buffer ) / GST_MSECOND );
        }
    }

    if ( timeMs >= 0 )
    {
        if ( state->firstPictureMs < 0 )
        {
            state->firstPictureMs = timeMs;
        }
        if ( timeMs > state->lastPictureMs )
        {
            state->lastPictureMs = timeMs;
            state->lastPictureEndMs = endMs;
        }
    }

    // A parser marks the frames that are not keyframes with the delta
    // unit flag, so only the others have to be looked at more closely.
    // That keeps the scan cheap on long files.
    bool candidate = !GST_BUFFER_FLAG_IS_SET(
        buffer, GST_BUFFER_FLAG_DELTA_UNIT );
    if ( candidate )
    {
        int type = NalParser::pictureNalType(
            codec, state->lengthSize, buffer, NAL_SCAN_LIMIT );
        // A recovery point is marked as a keyframe by the parser, but it
        // is not an entry point: only a real IDR or IRAP picture closes
        // a group of pictures.
        candidate = NalParser::isEntryPoint( codec, type );
    }

    if ( candidate && timeMs >= 0
         && ( state->times.isEmpty() || state->times.last() != timeMs ) )
    {
        state->times.append( timeMs );
        // The raw timestamp is stored as well: it is the only value the
        // decoder gate and the scan are guaranteed to agree on.
        state->pts.append( static_cast<qint64>(
            GST_BUFFER_PTS_IS_VALID( buffer ) ? GST_BUFFER_PTS( buffer )
                                              : 0 ) );
    }

    // The offset of the buffer tells how much of the file has been read,
    // which works for every container and needs no duration.
    if ( GST_BUFFER_OFFSET_IS_VALID( buffer ) )
    {
        state->bytesRead = static_cast<qint64>( GST_BUFFER_OFFSET( buffer ) );
    }
    gst_caps_unref( caps );
    return GST_PAD_PROBE_OK;
}

/*!
 * \brief Remembers the segment of the stream, which gives the timeline.
 */
GstPadProbeReturn onScanEvent( GstPad *_pad, GstPadProbeInfo *_info,
                               gpointer _data )
{
    ScanState *state = static_cast<ScanState *>( _data );
    GstEvent *event = GST_PAD_PROBE_INFO_EVENT( _info );
    if ( event == nullptr || GST_EVENT_TYPE( event ) != GST_EVENT_SEGMENT )
    {
        return GST_PAD_PROBE_OK;
    }
    const GstSegment *segment = gst_event_get_segment( event );
    if ( segment != nullptr )
    {
        QMutexLocker locker( &state->mutex );
        state->segment = *segment;
        state->haveSegment = true;
    }
    return GST_PAD_PROBE_OK;
}

/*!
 * \brief Attaches the probes to one stream of the file.
 */
void onScanPadAdded( GstElement *_element, GstPad *_pad, gpointer _data )
{
    Q_UNUSED( _element );
    ScanState *state = static_cast<ScanState *>( _data );
    gst_pad_add_probe( _pad, GST_PAD_PROBE_TYPE_BUFFER, onScanBuffer,
                       state, nullptr );
    gst_pad_add_probe( _pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
                       onScanEvent, state, nullptr );
}
}

KeyframeIndex::KeyframeIndex( QObject *_parent )
    : QObject( _parent ),
      m_progress( 0 ),
      m_scanning( false ),
      m_generation( 0 ),
      m_watchedGeneration( 0 )
{
    // The watcher lives in the GUI thread, so finished() is delivered
    // there even though the scan itself runs on a pool thread.
    connect( &m_watcher, &QFutureWatcher<SnapshotPointer>::finished,
             this, &KeyframeIndex::onScanFinished );
}

KeyframeIndex::~KeyframeIndex()
{
    // The pool thread only holds its own copies, so it may safely finish
    // after this object is gone; it just stops early.
    stop();
}

void KeyframeIndex::start( const QString &_filePath )
{
    stop();

    QFileInfo info( _filePath );
    if ( !info.exists() || !info.isFile() )
    {
        setError( "File not found: " + _filePath );
        return;
    }

    m_path = _filePath;
    m_progress = 0;
    m_errorString.clear();
    {
        QMutexLocker locker( &m_mutex );
        m_snapshot = SnapshotPointer( new Snapshot );
    }
    emit progressChanged();
    emit keyframeChanged();

    // A cache file of an earlier run makes the start immediate.
    QVector<qint64> cached;
    QVector<qint64> cachedPts;
    qint64 cachedDuration = 0;
    if ( readCache( _filePath, cached, cachedPts, cachedDuration ) )
    {
        SnapshotPointer snapshot( new Snapshot );
        snapshot->times = cached;
        snapshot->pts = cachedPts;
        snapshot->complete = true;
        snapshot->durationMs = cachedDuration;
        snapshot->largestGapMs = largestGap( cached );
        publish( snapshot );
        m_progress = 100;
        emit progressChanged();
        return;
    }

    m_cancelFlag = QSharedPointer<QAtomicInt>( new QAtomicInt( 0 ) );
    m_watchedGeneration = ++m_generation;
    quint64 generation = m_generation;

    // The callback runs on the pool thread. It builds a new immutable
    // snapshot and hands it to the GUI thread, which publishes it.
    ProgressCallback callback = [this, generation]( const ScanResult &_result )
    {
        SnapshotPointer snapshot( new Snapshot );
        snapshot->times = _result.times;
        snapshot->pts = _result.pts;
        snapshot->complete = _result.complete;
        snapshot->durationMs = _result.durationMs;
        snapshot->largestGapMs = _result.largestGapMs;
        QMetaObject::invokeMethod(
            this, [this, generation, snapshot, _result]()
            {
                if ( generation != m_generation )
                {
                    return;     // the file changed, the result is stale
                }
                publish( snapshot );
                if ( _result.percent != m_progress )
                {
                    m_progress = _result.percent;
                    emit progressChanged();
                }
            }, Qt::QueuedConnection );
    };

    m_watcher.setFuture( QtConcurrent::run( &KeyframeIndex::scanFile,
                                            _filePath, m_cancelFlag,
                                            callback ) );
    if ( !m_scanning )
    {
        m_scanning = true;
        emit scanningChanged();
    }
}

void KeyframeIndex::stop()
{
    // The generation is raised first, so a result that is already on its
    // way to the GUI thread is dropped when it arrives.
    ++m_generation;
    if ( !m_cancelFlag.isNull() )
    {
        m_cancelFlag->storeRelease( 1 );
    }
    // Wait for the pool thread, so no callback can run after this line.
    m_watcher.waitForFinished();
    m_cancelFlag.clear();
    if ( m_scanning )
    {
        m_scanning = false;
        emit scanningChanged();
    }
}

bool KeyframeIndex::isScanning() const
{
    return m_scanning;
}

int KeyframeIndex::progress() const
{
    return m_progress;
}

int KeyframeIndex::keyframeCount() const
{
    QMutexLocker locker( &m_mutex );
    return m_snapshot.isNull() ? 0 : m_snapshot->times.size();
}

bool KeyframeIndex::isComplete() const
{
    QMutexLocker locker( &m_mutex );
    return !m_snapshot.isNull() && m_snapshot->complete;
}

qint64 KeyframeIndex::durationMs() const
{
    QMutexLocker locker( &m_mutex );
    return m_snapshot.isNull() ? 0 : m_snapshot->durationMs;
}

qint64 KeyframeIndex::largestGapMs() const
{
    QMutexLocker locker( &m_mutex );
    return m_snapshot.isNull() ? 0 : m_snapshot->largestGapMs;
}

QString KeyframeIndex::path() const
{
    return m_path;
}

QString KeyframeIndex::errorString() const
{
    return m_errorString;
}

qint64 KeyframeIndex::entryTimeBefore( qint64 _timeMs ) const
{
    QMutexLocker locker( &m_mutex );
    if ( m_snapshot.isNull() || m_snapshot->times.isEmpty() )
    {
        return -1;
    }
    // The times are sorted, so the last one below the target is found
    // with a binary search.
    const QVector<qint64> &times = m_snapshot->times;
    int low = 0;
    int high = times.size() - 1;
    int found = -1;
    while ( low <= high )
    {
        int middle = ( low + high ) / 2;
        if ( times.at( middle ) <= _timeMs )
        {
            found = middle;
            low = middle + 1;
        }
        else
        {
            high = middle - 1;
        }
    }
    return found < 0 ? -1 : times.at( found );
}

qint64 KeyframeIndex::lastKnownTimeMs() const
{
    QMutexLocker locker( &m_mutex );
    if ( m_snapshot.isNull() || m_snapshot->times.isEmpty() )
    {
        return -1;
    }
    return m_snapshot->times.last();
}

QVector<qint64> KeyframeIndex::keyframeTimes() const
{
    QMutexLocker locker( &m_mutex );
    return m_snapshot.isNull() ? QVector<qint64>() : m_snapshot->times;
}

bool KeyframeIndex::entryPtsBefore( qint64 _timeMs, qint64 &ptsNs_ ) const
{
    QMutexLocker locker( &m_mutex );
    ptsNs_ = -1;
    if ( m_snapshot.isNull() || m_snapshot->times.isEmpty() )
    {
        return false;
    }
    // The times are sorted, so the last one below the target is found
    // with a binary search.
    const QVector<qint64> &times = m_snapshot->times;
    int low = 0;
    int high = times.size() - 1;
    int found = -1;
    while ( low <= high )
    {
        int middle = ( low + high ) / 2;
        if ( times.at( middle ) <= _timeMs )
        {
            found = middle;
            low = middle + 1;
        }
        else
        {
            high = middle - 1;
        }
    }
    if ( found < 0 )
    {
        return false;
    }
    // The raw timestamp is only known when the scan stored it. Without it
    // the gate cannot compare anything, so the seek works as before.
    if ( m_snapshot->pts.size() <= found )
    {
        return false;
    }
    ptsNs_ = m_snapshot->pts.at( found );
    return ptsNs_ > 0;
}

void KeyframeIndex::publish( const SnapshotPointer &_snapshot )
{
    {
        QMutexLocker locker( &m_mutex );
        m_snapshot = _snapshot;
    }
    emit keyframeChanged();
}

void KeyframeIndex::onScanFinished()
{
    if ( m_watchedGeneration != m_generation )
    {
        return;     // a newer scan has been started, this result is stale
    }
    SnapshotPointer result = m_watcher.result();
    if ( result.isNull() )
    {
        setError( "Scan cancelled" );
    }
    else
    {
        publish( result );
        if ( !result->errorString.isEmpty() )
        {
            setError( result->errorString );
        }
        if ( result->complete )
        {
            m_progress = 100;
            emit progressChanged();
            writeCache( m_path, result->times, result->pts,
                        result->durationMs );
        }
    }
    if ( m_scanning )
    {
        m_scanning = false;
        emit scanningChanged();
    }
}

void KeyframeIndex::setError( const QString &_text )
{
    if ( _text != m_errorString )
    {
        m_errorString = _text;
        emit scanningChanged();
    }
}

qint64 KeyframeIndex::largestGap( const QVector<qint64> &_times )
{
    qint64 largest = 0;
    for ( int i = 1; i < _times.size(); ++i )
    {
        largest = qMax( largest, _times.at( i ) - _times.at( i - 1 ) );
    }
    return largest;
}

KeyframeIndex::SnapshotPointer KeyframeIndex::scanFile(
    const QString &_filePath, QSharedPointer<QAtomicInt> _cancel,
    const ProgressCallback &_progress )
{
    SnapshotPointer result( new Snapshot );

    GError *error = nullptr;
    gchar *fileUri = gst_filename_to_uri( _filePath.toUtf8().constData(),
                                          &error );
    if ( fileUri == nullptr )
    {
        qWarning() << "Cannot open" << _filePath << error->message;
        g_error_free( error );
        result->errorString = QString( "Cannot open %1" ).arg( _filePath );
        return result;
    }

    // Only demux and parse, never decode: the file is read once and the
    // access units are inspected from the parser output.
    GstElement *pipeline = gst_pipeline_new( "keyframeScan" );
    GstElement *source = gst_element_factory_make( "filesrc", "source" );
    GstElement *parseBin = gst_element_factory_make( "parsebin", "parse" );
    GstElement *sink = gst_element_factory_make( "fakesink", "sink" );
    if ( pipeline == nullptr || source == nullptr || parseBin == nullptr
         || sink == nullptr )
    {
        result->errorString = QString( "Missing GStreamer elements" );
        if ( pipeline != nullptr )
        {
            gst_object_unref( pipeline );
        }
        g_free( fileUri );
        return result;
    }

    g_object_set( source, "location", fileUri, nullptr );
    // The scanner reads as fast as the disk allows.
    g_object_set( sink, "sync", FALSE, nullptr );

    GstBus *bus = gst_pipeline_get_bus( GST_PIPELINE( pipeline ) );
    gst_bus_set_sync_handler( bus, []( GstBus *, GstMessage *_message,
                                       gpointer ) -> GstBusSyncReply
    {
        // The scanner does not need the messages of the pipeline, but it
        // must not let them pile up. The end of the file and the errors
        // have to reach the bus, so they are passed on.
        GstMessageType type = GST_MESSAGE_TYPE( _message );
        if ( type == GST_MESSAGE_EOS || type == GST_MESSAGE_ERROR )
        {
            return GST_BUS_PASS;
        }
        gst_message_unref( _message );
        return GST_BUS_DROP;
    }, nullptr, nullptr );

    gst_bin_add_many( GST_BIN( pipeline ), source, parseBin, sink, nullptr );
    if ( !gst_element_link_many( source, parseBin, sink, nullptr ) )
    {
        result->errorString = QString( "Cannot link the scanner pipeline" );
        gst_object_unref( bus );
        gst_object_unref( pipeline );
        g_free( fileUri );
        return result;
    }
    g_free( fileUri );

    // The parsebin creates one pad per stream of the file. Only the
    // video stream is of interest, so every pad gets a probe that reads
    // the keyframes and drops the buffers of the other streams.
    ScanState state;
    state.cancel = _cancel;
    state.fileSize = QFileInfo( _filePath ).size();

    g_signal_connect( parseBin, "pad-added",
                      G_CALLBACK( &onScanPadAdded ), &state );

    gst_element_set_state( pipeline, GST_STATE_PLAYING );

    bool cancelled = false;
    while ( true )
    {
        if ( _cancel->loadAcquire() != 0 )
        {
            cancelled = true;
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
                state.failed = true;
                state.errorString =
                    QString::fromUtf8( messageError->message );
                g_error_free( messageError );
            }
            gst_message_unref( message );
            break;
        }

        // The progress follows the number of bytes read, which works for
        // every container and does not need a duration.
        int percent = state.percent;
        qint64 bytesRead = 0;
        {
            QMutexLocker locker( &state.mutex );
            bytesRead = state.bytesRead;
        }
        if ( state.fileSize > 0 )
        {
            percent = static_cast<int>( 100 * bytesRead / state.fileSize );
            percent = qBound( 0, percent, 99 );
        }
        if ( percent != state.percent )
        {
            state.percent = percent;
            ScanResult report;
            {
                QMutexLocker locker( &state.mutex );
                report.times = state.times;
                report.pts = state.pts;
                report.durationMs = state.lastPictureEndMs;
                report.largestGapMs = largestGap( state.times );
            }
            report.percent = percent;
            if ( _progress )
            {
                _progress( report );
            }
        }
    }

    gst_element_set_state( pipeline, GST_STATE_NULL );
    gst_object_unref( bus );
    gst_object_unref( pipeline );

    QVector<qint64> times;
    QVector<qint64> pts;
    bool haveVideoPad = false;
    qint64 lastPictureEndMs = -1;
    {
        QMutexLocker locker( &state.mutex );
        times = state.times;
        pts = state.pts;
        haveVideoPad = state.haveVideoPad;
        lastPictureEndMs = state.lastPictureEndMs;
    }

    if ( cancelled )
    {
        return SnapshotPointer();
    }
    if ( state.failed )
    {
        result->errorString = state.errorString;
        return result;
    }
    if ( !haveVideoPad )
    {
        result->errorString = QString( "No H.264 or H.265 video in %1" )
                                  .arg( _filePath );
        return result;
    }
    result->times = times;
    result->pts = pts;
    result->complete = true;
    // The exact duration of the file: the end of the last picture. A
    // container without an index only reports an estimate, which is often
    // too short and makes the handle of the slider drift.
    result->durationMs = ( lastPictureEndMs > 0 ) ? lastPictureEndMs : 0;
    result->largestGapMs = largestGap( times );
    if ( _progress )
    {
        ScanResult report;
        report.times = times;
        report.pts = pts;
        report.percent = 100;
        report.complete = true;
        report.durationMs = result->durationMs;
        report.largestGapMs = result->largestGapMs;
        _progress( report );
    }
    return result;
}

QString KeyframeIndex::cachePath( const QString &_filePath )
{
    QString suffix = QString::fromLatin1( CACHE_SUFFIX );
    QString candidate = _filePath + suffix;
    QFileInfo info( candidate );
    if ( info.absoluteDir().isWritable() )
    {
        return candidate;
    }
    // The folder of the video may be read only, so the cache goes to the
    // cache folder of the user.
    QString name = QFileInfo( _filePath ).fileName() + suffix;
    QString folder = QStandardPaths::writableLocation(
        QStandardPaths::CacheLocation );
    return folder + QLatin1Char( '/' ) + name;
}

bool KeyframeIndex::writeCache( const QString &_filePath,
                                const QVector<qint64> &_times,
                                const QVector<qint64> &_pts,
                                qint64 _durationMs )
{
    QFileInfo video( _filePath );
    QFile file( cachePath( _filePath ) );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    {
        return false;
    }

    file.write( CACHE_MAGIC, 8 );
    QDataStream stream( &file );
    stream.setVersion( QDataStream::Qt_5_12 );
    stream << static_cast<qint64>( video.size() );
    stream << static_cast<qint64>( video.lastModified().toMSecsSinceEpoch() );
    stream << static_cast<qint64>( _durationMs );
    stream << static_cast<qint32>( _times.size() );
    for ( qint64 time : _times )
    {
        stream << time;
    }
    // The raw timestamps follow. A cache file of an older version does
    // not have them, so the index then only knows the times.
    stream << static_cast<qint32>( _pts.size() );
    for ( qint64 pts : _pts )
    {
        stream << pts;
    }
    return file.flush();
}

bool KeyframeIndex::readCache( const QString &_filePath,
                               QVector<qint64> &times_,
                               QVector<qint64> &pts_,
                               qint64 &durationMs_ )
{
    QFileInfo video( _filePath );
    QFile file( cachePath( _filePath ) );
    if ( !file.open( QIODevice::ReadOnly ) )
    {
        return false;
    }
    QByteArray magic( 8, '\0' );
    if ( file.read( magic.data(), 8 ) != 8
         || memcmp( magic.constData(), CACHE_MAGIC, 8 ) != 0 )
    {
        return false;
    }

    QDataStream stream( &file );
    stream.setVersion( QDataStream::Qt_5_12 );
    qint64 size = 0;
    qint64 modified = 0;
    qint64 durationMs = 0;
    qint32 count = 0;
    stream >> size >> modified >> durationMs >> count;
    if ( stream.status() != QDataStream::Ok || count < 0 )
    {
        return false;
    }
    // The cache belongs to one video file: a different size or a newer
    // modification time means the video has changed.
    if ( size != static_cast<qint64>( video.size() )
         || modified != video.lastModified().toMSecsSinceEpoch() )
    {
        return false;
    }

    QVector<qint64> times;
    times.reserve( count );
    for ( qint32 i = 0; i < count; ++i )
    {
        qint64 time = 0;
        stream >> time;
        if ( stream.status() != QDataStream::Ok )
        {
            return false;
        }
        times.append( time );
    }

    // The raw timestamps are optional: a cache file without them is still
    // useful, the index then only knows the times of the keyframes.
    QVector<qint64> pts;
    qint32 ptsCount = 0;
    if ( stream.status() == QDataStream::Ok )
    {
        stream >> ptsCount;
        if ( stream.status() == QDataStream::Ok && ptsCount >= 0 )
        {
            pts.reserve( ptsCount );
            for ( qint32 i = 0; i < ptsCount; ++i )
            {
                qint64 value = 0;
                stream >> value;
                if ( stream.status() != QDataStream::Ok )
                {
                    break;
                }
                pts.append( value );
            }
        }
    }

    times_ = times;
    pts_ = pts;
    durationMs_ = durationMs;
    return true;
}
