#include "videoPlayer.hpp"

const double VideoPlayer::MIN_RATE = 0.25;
const double VideoPlayer::MAX_RATE = 4.0;

VideoPlayer::VideoPlayer( QObject *_parent )
    : QObject( _parent ),
      m_pipeline( nullptr ),
      m_appSink( nullptr ),
      m_generation( 0 ),
      m_playing( false ),
      m_position( 0 ),
      m_duration( 0 ),
      m_rate( 1.0 ),
      m_seekable( false ),
      m_atEnd( false ),
      m_seekInFlight( false ),
      m_pendingSeekMs( -1 ),
      m_pendingSeekAccurate( true ),
      m_stepBackFromTime( -1 ),
      m_stepBackFromId( -1 ),
      m_stepBackTries( -1 ),
      m_nominalFrameMs( DEFAULT_FRAME_MS ),
      m_currentFrameId( -1 ),
      m_currentFrameTime( -1 ),
      m_currentFrameEnd( -1 ),
      m_latestFrameEnd( -1 ),
      m_deliveredFrames( 0 ),
      m_unreadableFrames( 0 ),
      m_pendingFrameId( -1 ),
      m_pendingFrameTime( -1 ),
      m_pendingFrameEnd( -1 ),
      m_hasPendingFrame( false ),
      m_lastStoredPts( GST_CLOCK_TIME_NONE ),
      m_flushGeneration( 0 ),
      m_previewSeek( false ),
      m_previewInFlight( false ),
      m_accurateTarget( -1 ),
      m_accurateRetries( 0 ),
      m_entryPts( GST_CLOCK_TIME_NONE ),
      m_entryToleranceNs( 0 ),
      m_keyframeIndex( nullptr ),
      m_useKeyframeIndex( true ),
      m_decoderBuffers( 0 )
{
    // GStreamer has no "position changed" message, so the position is
    // polled. A 100 ms GUI timer is cheap and keeps the slider smooth.
    m_positionTimer.setInterval( POSITION_INTERVAL_MS );
    connect( &m_positionTimer, &QTimer::timeout,
             this, &VideoPlayer::updatePosition );
}

VideoPlayer::~VideoPlayer()
{
    closePipeline();
}

bool VideoPlayer::open( const QUrl &_url )
{
    closePipeline();
    setError( QString() );

    QByteArray uri;
    if ( _url.isLocalFile() )
    {
        if ( !QFileInfo::exists( _url.toLocalFile() ) )
        {
            setError( "File not found: " + _url.toLocalFile() );
            return false;
        }
        GError *error = nullptr;
        gchar *fileUri = gst_filename_to_uri(
            _url.toLocalFile().toUtf8().constData(), &error );
        if ( fileUri == nullptr )
        {
            setError( QString::fromUtf8( error->message ) );
            g_error_free( error );
            return false;
        }
        uri = fileUri;
        g_free( fileUri );
    }
    else
    {
        uri = _url.toString().toUtf8();
    }

    // playbin picks the demuxer and the decoder from the installed plugins,
    // so no container or codec is hard-coded here.
    m_pipeline = gst_element_factory_make( "playbin", "player" );
    if ( m_pipeline == nullptr )
    {
        setError( "GStreamer element 'playbin' is not available" );
        return false;
    }
    // Every decoder playbin creates gets a keyframe gate (onDecoderInput)
    g_signal_connect( m_pipeline, "deep-element-added",
                      G_CALLBACK( &VideoPlayer::onDeepElementAdded ), this );

    // Every pixel format is converted to RGBA so the rest of the program
    // only has to handle one layout.
    GError *error = nullptr;
    GstElement *videoSink = gst_parse_bin_from_description(
        "videoconvert ! appsink name=frameSink "
        "caps=video/x-raw,format=RGBA max-buffers=1 drop=true",
        TRUE, &error );
    if ( videoSink == nullptr )
    {
        setError( QString::fromUtf8( error->message ) );
        g_error_free( error );
        closePipeline();
        return false;
    }
    m_appSink = gst_bin_get_by_name( GST_BIN( videoSink ), "frameSink" );

    // A flushing seek sends FLUSH_STOP through the sink exactly between the
    // data from before and after the seek: the place to drop stale frames.
    GstPad *sinkPad = gst_element_get_static_pad( m_appSink, "sink" );
    gst_pad_add_probe( sinkPad, GST_PAD_PROBE_TYPE_EVENT_FLUSH,
                       &VideoPlayer::onFlush,
                       new CallbackContext{ this, m_generation + 1 },
                       &VideoPlayer::freeContext );
    gst_object_unref( sinkPad );

    ++m_generation;

    GstAppSinkCallbacks callbacks;
    memset( &callbacks, 0, sizeof( callbacks ) );
    callbacks.new_sample = &VideoPlayer::onNewSample;
    callbacks.new_preroll = &VideoPlayer::onNewPreroll;
    gst_app_sink_set_callbacks( GST_APP_SINK( m_appSink ), &callbacks,
                                new CallbackContext{ this, m_generation },
                                &VideoPlayer::freeContext );

    g_object_set( m_pipeline, "uri", uri.constData(),
                  "video-sink", videoSink, nullptr );

    GstBus *bus = gst_element_get_bus( m_pipeline );
    gst_bus_set_sync_handler( bus, &VideoPlayer::onBusSync,
                              new CallbackContext{ this, m_generation },
                              &VideoPlayer::freeContext );
    gst_object_unref( bus );

    m_source = QString::fromUtf8( uri );
    emit sourceChanged();

    // PAUSED prerolls the pipeline: the first frame is decoded and shown,
    // and the duration becomes known, without starting playback.
    if ( gst_element_set_state( m_pipeline, GST_STATE_PAUSED )
         == GST_STATE_CHANGE_FAILURE )
    {
        setError( "Cannot open " + m_source );
        closePipeline();
        return false;
    }
    m_positionTimer.start();
    return true;
}

void VideoPlayer::setKeyframeIndex( KeyframeIndex *_index )
{
    m_keyframeIndex = _index;
}

KeyframeIndex *VideoPlayer::keyframeIndex() const
{
    return m_keyframeIndex;
}

void VideoPlayer::refreshDuration()
{
    updateDuration();
}

void VideoPlayer::setUseKeyframeIndex( bool _enabled )
{
    if ( _enabled == m_useKeyframeIndex )
    {
        return;
    }
    m_useKeyframeIndex = _enabled;
    emit useKeyframeIndexChanged();
}

bool VideoPlayer::useKeyframeIndex() const
{
    return m_useKeyframeIndex;
}

void VideoPlayer::play()
{
    if ( m_pipeline == nullptr )
    {
        return;
    }
    if ( m_atEnd )
    {
        seek( 0, true );    // play again from the start
    }
    gst_element_set_state( m_pipeline, GST_STATE_PLAYING );
}

void VideoPlayer::pause()
{
    if ( m_pipeline != nullptr )
    {
        gst_element_set_state( m_pipeline, GST_STATE_PAUSED );
    }
}

void VideoPlayer::togglePlay()
{
    if ( m_playing )
    {
        pause();
    }
    else
    {
        play();
    }
}

void VideoPlayer::seek( qint64 _positionMs, bool _accurate )
{
    if ( m_pipeline == nullptr || !m_seekable )
    {
        return;
    }
    // The duration is where the last frame ends, so clamp to 1 ms before
    // it: a seek to the very end finds no frame to show.
    qint64 target = qMax<qint64>( 0, _positionMs );
    if ( m_duration > 0 )
    {
        target = qMin( target, m_duration - 1 );
    }

    // A seek is still running: remember only the newest request. The
    // timeout protects against a seek whose ASYNC_DONE never arrives.
    // An accurate seek (slider released) does not wait for a preview: it
    // flushes it away at once.
    bool interruptPreview = _accurate && m_previewInFlight;
    if ( m_seekInFlight && m_seekClock.elapsed() < SEEK_TIMEOUT_MS
         && !interruptPreview )
    {
        m_pendingSeekMs = target;
        m_pendingSeekAccurate = _accurate;
        return;
    }
    m_pendingSeekMs = -1;
    m_accurateRetries = 0;
    startSeek( target, _accurate );
}

void VideoPlayer::seekRelative( qint64 _deltaMs )
{
    qint64 base = ( m_currentFrameTime >= 0 ) ? m_currentFrameTime
                                              : m_position;
    seek( base + _deltaMs, true );
}

void VideoPlayer::stepForward()
{
    if ( m_pipeline == nullptr || m_atEnd )
    {
        return;
    }
    if ( m_playing )
    {
        pause();    // the frame that prerolls on pause is the next one
        return;
    }
    // In PAUSED, a step event advances the sink by exactly one buffer,
    // which then arrives through the preroll callback.
    gst_element_send_event( m_pipeline,
                            gst_event_new_step( GST_FORMAT_BUFFERS, 1, 1.0,
                                                TRUE, FALSE ) );
}

void VideoPlayer::stepBackward()
{
    if ( m_pipeline == nullptr || m_currentFrameTime <= 0
         || m_stepBackTries >= 0 )
    {
        return;     // nothing before, or a backward step is still running
    }
    pause();
    // Seek to 1 ms before the current frame. The previous frame normally
    // covers that instant; when it does not (a gap left by a dropped
    // frame, or a frame cut by an earlier seek), onSeekDone() retries a
    // little further back.
    m_stepBackFromTime = m_currentFrameTime;
    m_stepBackFromId = m_currentFrameId;
    m_stepBackTries = 0;
    updateNominalFrameDuration();
    seek( m_stepBackFromTime - 1, true );
}

void VideoPlayer::setRate( double _rate )
{
    double rate = qBound( MIN_RATE, _rate, MAX_RATE );
    if ( qFuzzyCompare( rate, m_rate ) )
    {
        return;
    }
    m_rate = rate;
    emit rateChanged();
    // GStreamer 1.16 changes the rate through a seek from the current frame
    if ( m_pipeline != nullptr && m_currentFrameTime >= 0 )
    {
        seek( m_currentFrameTime, true );
    }
}

double VideoPlayer::rate() const
{
    return m_rate;
}

bool VideoPlayer::isSeekable() const
{
    return m_seekable;
}

bool VideoPlayer::isPlaying() const
{
    return m_playing;
}

qint64 VideoPlayer::position() const
{
    return m_position;
}

qint64 VideoPlayer::duration() const
{
    return m_duration;
}

QString VideoPlayer::source() const
{
    return m_source;
}

QString VideoPlayer::errorString() const
{
    return m_errorString;
}

QImage VideoPlayer::currentFrame() const
{
    return m_currentFrame;
}

quint64 VideoPlayer::deliveredFrames() const
{
    return m_deliveredFrames;
}

qint64 VideoPlayer::frameId() const
{
    return m_currentFrameId;
}

qint64 VideoPlayer::frameTime() const
{
    return m_currentFrameTime;
}

quint64 VideoPlayer::unreadableFrames() const
{
    return m_unreadableFrames;
}

quint64 VideoPlayer::decoderBuffers() const
{
    return m_decoderBuffers.load();
}

QVariantMap VideoPlayer::videoInfo() const
{
    QVariantMap info = m_videoInfo;
    QString codec;
    {
        QMutexLocker locker( &m_codecMutex );
        codec = m_videoCodec;
    }
    // The name of the codec is written by a streaming thread, so it can
    // be newer than the rest of the map.
    info.insert( "codec", codec.isEmpty() ? QString( "unknown" ) : codec );
    return info;
}

void VideoPlayer::closePipeline()
{
    m_positionTimer.stop();
    if ( m_pipeline != nullptr )
    {
        // Remove the bus handler first: messages posted while shutting
        // down must not be queued to this object any more.
        GstBus *bus = gst_element_get_bus( m_pipeline );
        gst_bus_set_sync_handler( bus, nullptr, nullptr, nullptr );
        gst_object_unref( bus );

        // Blocks until every streaming thread has stopped, so no appsink
        // callback can run after this line.
        gst_element_set_state( m_pipeline, GST_STATE_NULL );

        if ( m_appSink != nullptr )
        {
            gst_object_unref( m_appSink );
            m_appSink = nullptr;
        }
        gst_object_unref( m_pipeline );
        m_pipeline = nullptr;
    }

    {
        QMutexLocker locker( &m_frameMutex );
        m_pendingFrame = QImage();
        m_pendingFrameId = -1;
        m_pendingFrameTime = -1;
        m_pendingFrameEnd = -1;
        m_hasPendingFrame = false;
        m_lastStoredPts = GST_CLOCK_TIME_NONE;
    }

    // Any queued bus message of the old pipeline is now stale
    ++m_generation;

    m_currentFrame = QImage();
    m_currentFrameId = -1;
    m_currentFrameTime = -1;
    m_currentFrameEnd = -1;
    m_latestFrameEnd = -1;
    m_deliveredFrames = 0;
    m_unreadableFrames = 0;
    m_decoderBuffers = 0;
    // The entry point belongs to the file that has just been closed.
    m_entryPts = GST_CLOCK_TIME_NONE;
    m_entryToleranceNs = 0;
    if ( m_playing )
    {
        m_playing = false;
        emit playingChanged();
    }
    m_position = 0;
    m_duration = 0;
    m_atEnd = false;
    m_seekInFlight = false;
    m_previewInFlight = false;
    m_pendingSeekMs = -1;
    m_stepBackTries = -1;
    m_accurateTarget = -1;
    if ( m_seekable )
    {
        m_seekable = false;
        emit seekableChanged();
    }
    emit positionChanged();
    emit durationChanged();
    emit frameReady();
}

void VideoPlayer::deliverFrame()
{
    {
        QMutexLocker locker( &m_frameMutex );
        if ( !m_hasPendingFrame )
        {
            return;
        }
        // QImage is implicitly shared: this only moves a reference, the
        // pixel data was already deep-copied on the streaming thread.
        m_currentFrame = m_pendingFrame;
        m_currentFrameId = m_pendingFrameId;
        m_currentFrameTime = m_pendingFrameTime;
        m_currentFrameEnd = m_pendingFrameEnd;
        m_pendingFrame = QImage();
        m_hasPendingFrame = false;
    }
    ++m_deliveredFrames;
    if ( m_currentFrameId < 0 )
    {
        ++m_unreadableFrames;
    }
    if ( m_currentFrameEnd > m_latestFrameEnd )
    {
        m_latestFrameEnd = m_currentFrameEnd;
        updateDuration();
    }
    emit frameReady();
}

void VideoPlayer::handleBusMessage( quint64 _generation,
                                    GstMessage *_message )
{
    if ( _generation != m_generation )
    {
        return;     // message of a pipeline that has been closed
    }

    switch ( GST_MESSAGE_TYPE( _message ) )
    {
    case GST_MESSAGE_ERROR:
    {
        GError *error = nullptr;
        gchar *debug = nullptr;
        gst_message_parse_error( _message, &error, &debug );
        // Keep the first error: later ones are usually consequences of it
        // (e.g. "Internal data stream error" after a failed type find).
        if ( m_errorString.isEmpty() )
        {
            setError( QString::fromUtf8( error->message ) );
        }
        qWarning() << "GStreamer error:" << error->message
                   << ( debug != nullptr ? debug : "" );
        g_error_free( error );
        g_free( debug );
        m_seekInFlight = false;
        m_pendingSeekMs = -1;
        m_stepBackTries = -1;
        pause();
        break;
    }
    case GST_MESSAGE_EOS:
    {
        // Stay on the last frame instead of going back to the start
        m_atEnd = true;
        pause();
        break;
    }
    case GST_MESSAGE_STATE_CHANGED:
    {
        GstState oldState;
        GstState newState;
        gst_message_parse_state_changed( _message, &oldState, &newState,
                                         nullptr );
        bool playing = ( newState == GST_STATE_PLAYING );
        if ( playing != m_playing )
        {
            m_playing = playing;
            emit playingChanged();
        }
        updateDuration();
        break;
    }
    case GST_MESSAGE_ASYNC_DONE:
    {
        // Posted when the pipeline has prerolled: after opening and after
        // every flushing seek.
        updateSeekable();
        updateDuration();
        updatePosition();
        updateVideoInfo();
        onSeekDone();
        break;
    }
    case GST_MESSAGE_DURATION_CHANGED:
    {
        updateDuration();
        updatePosition();
        break;
    }
    default:
    {
        break;
    }
    }
}

void VideoPlayer::updatePosition()
{
    if ( m_pipeline == nullptr )
    {
        return;
    }
    gint64 position = 0;
    if ( gst_element_query_position( m_pipeline, GST_FORMAT_TIME,
                                     &position ) )
    {
        qint64 positionMs = position / GST_MSECOND;
        // The position of the pipeline can be a little ahead of the
        // picture on screen, and it can be behind it when the video is
        // paused. The time of the picture is the truth, so it is used
        // when the pipeline is not playing.
        if ( !m_playing && m_currentFrameTime >= 0 )
        {
            positionMs = m_currentFrameTime;
        }
        // A container without an index sometimes reports a position
        // that is past the end of the file, which would move the handle
        // of the slider past the end.
        if ( m_duration > 0 && positionMs > m_duration )
        {
            positionMs = m_duration;
        }
        if ( positionMs < 0 )
        {
            positionMs = 0;
        }
        if ( positionMs != m_position )
        {
            m_position = positionMs;
            emit positionChanged();
        }
    }
}

void VideoPlayer::updateDuration()
{
    if ( m_pipeline == nullptr )
    {
        return;
    }
    // Containers without an index (MPEG-TS, raw streams) only report an
    // estimated duration, which can be shorter than the real stream. The
    // end of the latest frame seen is a hard lower bound, so the time on
    // screen can never run past the duration.
    qint64 durationMs = m_latestFrameEnd;
    gint64 duration = 0;
    if ( gst_element_query_duration( m_pipeline, GST_FORMAT_TIME,
                                     &duration ) )
    {
        durationMs = qMax( durationMs,
                           static_cast<qint64>( duration / GST_MSECOND ) );
    }

    // The keyframe index knows the exact duration: the end of the last
    // picture of the file. It is the best value available, so it wins.
    if ( m_keyframeIndex != nullptr )
    {
        qint64 exact = m_keyframeIndex->durationMs();
        if ( exact > 0 )
        {
            durationMs = exact;
        }
        else if ( m_keyframeIndex->isScanning() )
        {
            // The scan has not reached the end of the file yet, but it
            // knows how far it has read. That time is a better lower
            // bound than the end of the last picture, because the
            // estimate of the demuxer is often too short.
            qint64 known = m_keyframeIndex->lastKnownTimeMs();
            if ( known > 0 )
            {
                durationMs = qMax( durationMs, known );
            }
        }
    }

    // The estimate of the demuxer changes while the file is read, and it
    // often becomes shorter. A duration that shrinks makes the handle of
    // the slider drift, so it only grows.
    if ( durationMs > 0 && durationMs != m_duration )
    {
        if ( durationMs < m_duration && m_duration > 0 )
        {
            return;
        }
        m_duration = durationMs;
        emit durationChanged();
    }
}

void VideoPlayer::startSeek( qint64 _positionMs, bool _accurate )
{
    // FLUSH discards everything queued in the pipeline, so the new
    // position shows up at once. ACCURATE decodes from the keyframe before
    // the target and drops frames until it; KEY_UNIT stops at a keyframe.
    GstSeekFlags flags = static_cast<GstSeekFlags>(
        GST_SEEK_FLAG_FLUSH
        | ( _accurate ? GST_SEEK_FLAG_ACCURATE
                      : ( GST_SEEK_FLAG_KEY_UNIT
                          | GST_SEEK_FLAG_SNAP_NEAREST ) ) );
    // Tell the decoder gates which kind of seek the coming flush belongs to
    m_previewSeek = !_accurate;
    // The keyframe index tells which picture the decoder has to start
    // with. The demuxer of a container without an index usually starts
    // several groups of pictures too early, and the decoder then decodes
    // hundreds of pictures that are thrown away again.
    updateEntryPoint( _positionMs, _accurate );
    bool ok = gst_element_seek( m_pipeline, m_rate, GST_FORMAT_TIME, flags,
                                GST_SEEK_TYPE_SET,
                                _positionMs * GST_MSECOND,
                                GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE );
    if ( !ok )
    {
        qWarning() << "Seek to" << _positionMs << "ms failed";
        return;
    }
    m_atEnd = false;
    m_seekInFlight = true;
    m_previewInFlight = !_accurate;
    m_accurateTarget = _accurate ? _positionMs : -1;
    m_seekClock.start();
}

void VideoPlayer::updateEntryPoint( qint64 _positionMs, bool _accurate )
{
    // Without an index, or for a preview seek, the gate works as before.
    m_entryPts = GST_CLOCK_TIME_NONE;
    m_entryToleranceNs = 0;
    if ( !_accurate || !m_useKeyframeIndex || m_keyframeIndex == nullptr )
    {
        return;
    }

    // The index only knows the part of the file that has been scanned.
    // A target beyond that point cannot be served from the table.
    qint64 lastKnown = m_keyframeIndex->lastKnownTimeMs();
    if ( lastKnown < 0 || _positionMs > lastKnown )
    {
        return;
    }

    qint64 ptsNs = -1;
    if ( !m_keyframeIndex->entryPtsBefore( _positionMs, ptsNs ) )
    {
        return;
    }

    // The gate compares the raw timestamp of a picture with this value.
    // A small tolerance covers the difference between the moment the
    // index was built and the moment the picture is decoded.
    m_entryToleranceNs = static_cast<gint64>( 2 * GST_MSECOND );
    m_entryPts = static_cast<guint64>( ptsNs );
}

void VideoPlayer::updateNominalFrameDuration()
{
    // Read the frame rate from the caps negotiated on the appsink pad
    m_nominalFrameMs = DEFAULT_FRAME_MS;
    GstPad *pad = gst_element_get_static_pad( m_appSink, "sink" );
    GstCaps *caps = gst_pad_get_current_caps( pad );
    gst_object_unref( pad );
    if ( caps == nullptr )
    {
        return;
    }
    GstVideoInfo info;
    if ( gst_video_info_from_caps( &info, caps )
         && GST_VIDEO_INFO_FPS_N( &info ) > 0 )
    {
        m_nominalFrameMs = qMax<qint64>(
            1, 1000 * GST_VIDEO_INFO_FPS_D( &info )
                   / GST_VIDEO_INFO_FPS_N( &info ) );
    }
    gst_caps_unref( caps );
}

void VideoPlayer::updateVideoInfo()
{
    if ( m_pipeline == nullptr || m_appSink == nullptr )
    {
        return;
    }

    QVariantMap info;
    GstPad *pad = gst_element_get_static_pad( m_appSink, "sink" );
    GstCaps *caps = gst_pad_get_current_caps( pad );
    gst_object_unref( pad );
    if ( caps != nullptr )
    {
        // The codec is not part of the caps of the raw picture, so it is
        // taken from the caps that the decoder negotiated.
        GstStructure *structure = gst_caps_get_structure( caps, 0 );
        if ( structure != nullptr )
        {
            GstVideoInfo videoInfo;
            if ( gst_video_info_from_caps( &videoInfo, caps ) )
            {
                info.insert( "width", GST_VIDEO_INFO_WIDTH( &videoInfo ) );
                info.insert( "height", GST_VIDEO_INFO_HEIGHT( &videoInfo ) );
                if ( GST_VIDEO_INFO_FPS_N( &videoInfo ) > 0 )
                {
                    double fps = static_cast<double>(
                        GST_VIDEO_INFO_FPS_N( &videoInfo ) )
                                 / GST_VIDEO_INFO_FPS_D( &videoInfo );
                    info.insert( "fps", fps );
                }
            }
        }
        gst_caps_unref( caps );
    }

    QString codec;
    {
        QMutexLocker locker( &m_codecMutex );
        codec = m_videoCodec;
    }
    codec = codec.isEmpty() ? QString( "unknown" ) : codec;
    info.insert( "codec", codec );

    if ( info == m_videoInfo )
    {
        return;
    }
    m_videoInfo = info;
    emit videoInfoChanged();
}

void VideoPlayer::onSeekDone()
{
    m_seekInFlight = false;
    m_previewInFlight = false;
    if ( m_pendingSeekMs >= 0 )
    {
        // A newer request wins, including over a backward step
        m_stepBackTries = -1;
        qint64 target = m_pendingSeekMs;
        m_pendingSeekMs = -1;
        m_accurateRetries = 0;
        startSeek( target, m_pendingSeekAccurate );
        return;
    }

    // Verify where an accurate seek landed. The MPEG-TS demuxer sometimes
    // starts inside a keyframe; the decoder then cannot rebuild the
    // pictures and the first frame shown is the next keyframe, seconds
    // after the target. Repeating the same seek normally succeeds.
    // While playing fast, a few frames may already have passed: allow for
    // the playback rate.
    updateNominalFrameDuration();
    qint64 allowedMs = static_cast<qint64>(
        MAX_LANDING_FRAMES * m_nominalFrameMs * qMax( 1.0, m_rate ) );
    if ( m_accurateTarget >= 0 && m_currentFrameTime >= 0
         && m_currentFrameTime > m_accurateTarget + allowedMs
         && m_accurateRetries < MAX_ACCURATE_RETRIES )
    {
        ++m_accurateRetries;
        qWarning() << "Accurate seek to" << m_accurateTarget
                   << "ms landed at" << m_currentFrameTime << "ms, retrying";
        startSeek( m_accurateTarget, true );
        return;
    }
    m_accurateTarget = -1;
    if ( m_stepBackTries < 0 )
    {
        return;
    }

    // The preroll frame of this seek was queued before ASYNC_DONE, so it
    // is already the current frame here. Did we really move back?
    bool movedBack = ( m_currentFrameId >= 0 && m_stepBackFromId >= 0 )
                     ? ( m_currentFrameId < m_stepBackFromId )
                     : ( m_currentFrameTime < m_stepBackFromTime );
    if ( movedBack || ++m_stepBackTries > MAX_STEP_BACK_TRIES )
    {
        m_stepBackTries = -1;
        return;
    }
    // Go back in half-frame increments: a frame lasts at least one nominal
    // frame duration, so no frame can be jumped over and the first earlier
    // frame found is the previous one. The frame on screen cannot give the
    // duration: a seek may have cut it short.
    qint64 increment = qMax<qint64>( 1, m_nominalFrameMs / 2 );
    qint64 target = m_stepBackFromTime - 1 - m_stepBackTries * increment;
    if ( target < 0 )
    {
        m_stepBackTries = -1;
        return;
    }
    startSeek( target, true );
}

void VideoPlayer::updateSeekable()
{
    if ( m_pipeline == nullptr )
    {
        return;
    }
    gboolean seekable = FALSE;
    GstQuery *query = gst_query_new_seeking( GST_FORMAT_TIME );
    if ( gst_element_query( m_pipeline, query ) )
    {
        gst_query_parse_seeking( query, nullptr, &seekable, nullptr,
                                 nullptr );
    }
    gst_query_unref( query );
    bool value = ( seekable == TRUE );
    if ( value != m_seekable )
    {
        m_seekable = value;
        emit seekableChanged();
    }
}

void VideoPlayer::setError( const QString &_text )
{
    if ( _text != m_errorString )
    {
        m_errorString = _text;
        emit errorChanged();
    }
}

void VideoPlayer::storeFrame( GstSample *_sample )
{
    // Runs on a GStreamer streaming thread. The deep copy and the bar
    // reading (tens of microseconds) are done here, outside the lock, so
    // the lock is held only to swap the results in.

    // When the pipeline pauses, the next buffer is handed over as a
    // preroll sample, then the very same buffer again when playback
    // resumes. Skip the second copy, before doing any expensive work.
    GstBuffer *sampleBuffer = gst_sample_get_buffer( _sample );
    quint64 flushGeneration = 0;
    {
        QMutexLocker locker( &m_frameMutex );
        flushGeneration = m_flushGeneration;
        if ( sampleBuffer != nullptr
             && GST_BUFFER_PTS_IS_VALID( sampleBuffer ) )
        {
            if ( GST_BUFFER_PTS( sampleBuffer ) == m_lastStoredPts )
            {
                return;
            }
            m_lastStoredPts = GST_BUFFER_PTS( sampleBuffer );
        }
    }

    QImage frame = sampleToImage( _sample );
    if ( frame.isNull() )
    {
        return;
    }
    qint64 frameId = -1;
    if ( !BarcodeReader::read( frame, frameId ) )
    {
        frameId = -1;
    }

    // Time of this frame on the stream timeline, from its own timestamp.
    // Unlike a position query, it cannot run ahead of the displayed frame.
    qint64 frameTime = -1;
    qint64 frameEnd = -1;
    GstBuffer *buffer = gst_sample_get_buffer( _sample );
    GstSegment *segment = gst_sample_get_segment( _sample );
    if ( buffer != nullptr && segment != nullptr
         && GST_BUFFER_PTS_IS_VALID( buffer ) )
    {
        guint64 streamTime = gst_segment_to_stream_time(
            segment, GST_FORMAT_TIME, GST_BUFFER_PTS( buffer ) );
        if ( GST_CLOCK_TIME_IS_VALID( streamTime ) )
        {
            frameTime = static_cast<qint64>( streamTime / GST_MSECOND );
            frameEnd = frameTime;
            if ( GST_BUFFER_DURATION_IS_VALID( buffer ) )
            {
                frameEnd += static_cast<qint64>(
                    GST_BUFFER_DURATION( buffer ) / GST_MSECOND );
            }
        }
    }

    bool needNotify = false;
    {
        QMutexLocker locker( &m_frameMutex );
        if ( flushGeneration != m_flushGeneration )
        {
            return;     // a seek happened while this frame was processed
        }
        m_pendingFrame = frame;
        m_pendingFrameId = frameId;
        m_pendingFrameTime = frameTime;
        m_pendingFrameEnd = frameEnd;
        needNotify = !m_hasPendingFrame;
        m_hasPendingFrame = true;
    }

    // At most one delivery is queued at a time: if the GUI has not taken
    // the previous frame yet, it will simply take this newer one instead.
    if ( needNotify )
    {
        QMetaObject::invokeMethod( this, [this]() { deliverFrame(); },
                                   Qt::QueuedConnection );
    }
}

QImage VideoPlayer::sampleToImage( GstSample *_sample )
{
    GstCaps *caps = gst_sample_get_caps( _sample );
    GstBuffer *buffer = gst_sample_get_buffer( _sample );
    GstVideoInfo info;
    if ( caps == nullptr || buffer == nullptr
         || !gst_video_info_from_caps( &info, caps ) )
    {
        return QImage();
    }

    GstVideoFrame videoFrame;
    if ( !gst_video_frame_map( &videoFrame, &info, buffer, GST_MAP_READ ) )
    {
        return QImage();
    }

    // Wrap the GStreamer memory, then deep-copy it: the buffer goes back
    // to GStreamer as soon as it is unmapped.
    QImage wrapped(
        static_cast<const uchar *>( GST_VIDEO_FRAME_PLANE_DATA( &videoFrame,
                                                                0 ) ),
        GST_VIDEO_FRAME_WIDTH( &videoFrame ),
        GST_VIDEO_FRAME_HEIGHT( &videoFrame ),
        GST_VIDEO_FRAME_PLANE_STRIDE( &videoFrame, 0 ),
        QImage::Format_RGBA8888 );
    QImage copy = wrapped.copy();
    gst_video_frame_unmap( &videoFrame );
    return copy;
}

GstFlowReturn VideoPlayer::onNewSample( GstAppSink *_sink, gpointer _data )
{
    CallbackContext *context = static_cast<CallbackContext *>( _data );
    GstSample *sample = gst_app_sink_pull_sample( _sink );
    if ( sample != nullptr )
    {
        context->player->storeFrame( sample );
        gst_sample_unref( sample );
    }
    return GST_FLOW_OK;
}

GstFlowReturn VideoPlayer::onNewPreroll( GstAppSink *_sink, gpointer _data )
{
    // Preroll frames arrive while paused: after opening and after a seek.
    CallbackContext *context = static_cast<CallbackContext *>( _data );
    GstSample *sample = gst_app_sink_pull_preroll( _sink );
    if ( sample != nullptr )
    {
        context->player->storeFrame( sample );
        gst_sample_unref( sample );
    }
    return GST_FLOW_OK;
}

GstBusSyncReply VideoPlayer::onBusSync( GstBus *_bus, GstMessage *_message,
                                        gpointer _data )
{
    Q_UNUSED( _bus );
    // Runs on whichever thread posted the message. Only the messages the
    // GUI needs are forwarded; state changes of inner elements are noise.
    CallbackContext *context = static_cast<CallbackContext *>( _data );
    VideoPlayer *player = context->player;
    quint64 generation = context->generation;

    GstMessageType type = GST_MESSAGE_TYPE( _message );
    bool fromPipeline = GST_IS_PIPELINE( GST_MESSAGE_SRC( _message ) );
    bool wanted = ( type == GST_MESSAGE_ERROR )
                  || ( type == GST_MESSAGE_EOS )
                  || ( type == GST_MESSAGE_DURATION_CHANGED )
                  || ( type == GST_MESSAGE_ASYNC_DONE )
                  || ( type == GST_MESSAGE_STATE_CHANGED && fromPipeline );
    if ( !wanted )
    {
        gst_message_unref( _message );
        return GST_BUS_DROP;
    }

    // Returning GST_BUS_DROP hands the message reference to us; the queued
    // call releases it on the GUI thread.
    QMetaObject::invokeMethod( player, [player, generation, _message]()
    {
        player->handleBusMessage( generation, _message );
        gst_message_unref( _message );
    }, Qt::QueuedConnection );
    return GST_BUS_DROP;
}

GstPadProbeReturn VideoPlayer::onFlush( GstPad *_pad, GstPadProbeInfo *_info,
                                        gpointer _data )
{
    Q_UNUSED( _pad );
    // Runs on the thread that performs the seek. GStreamer delivers
    // FLUSH_STOP to the sink only after the previous buffer has been fully
    // handled, so every frame from before the seek is already stored or
    // being dropped by the generation check.
    GstEvent *event = GST_PAD_PROBE_INFO_EVENT( _info );
    if ( event != nullptr && GST_EVENT_TYPE( event ) == GST_EVENT_FLUSH_STOP )
    {
        VideoPlayer *player = static_cast<CallbackContext *>( _data )->player;
        QMutexLocker locker( &player->m_frameMutex );
        ++player->m_flushGeneration;
        player->m_pendingFrame = QImage();
        player->m_pendingFrameId = -1;
        player->m_hasPendingFrame = false;
        // The frame shown before the seek may legitimately come again
        player->m_lastStoredPts = GST_CLOCK_TIME_NONE;
    }
    return GST_PAD_PROBE_OK;
}

void VideoPlayer::freeContext( gpointer _data )
{
    delete static_cast<CallbackContext *>( _data );
}

void VideoPlayer::onDeepElementAdded( GstBin *_bin, GstBin *_subBin,
                                      GstElement *_element, gpointer _data )
{
    Q_UNUSED( _bin );
    Q_UNUSED( _subBin );
    // Runs on the thread that builds the decoding chain (a streaming
    // thread). Only video decoders are of interest.
    if ( !GST_IS_VIDEO_DECODER( _element ) )
    {
        return;
    }
    // libav decoders: do not show pictures decoded with missing references
    // until told otherwise by a seek (see onDecoderInput).
    bool hasOutputCorrupt = g_object_class_find_property(
                                G_OBJECT_GET_CLASS( _element ),
                                "output-corrupt" ) != nullptr;
    if ( hasOutputCorrupt )
    {
        g_object_set( _element, "output-corrupt", FALSE, nullptr );
    }

    GstPad *pad = gst_element_get_static_pad( _element, "sink" );
    if ( pad == nullptr )
    {
        return;
    }
    DecoderGate *gate = new DecoderGate;
    gate->player = static_cast<VideoPlayer *>( _data );
    gate->decoder = _element;
    gate->hasOutputCorrupt = hasOutputCorrupt;
    gate->codec = NalParser::CODEC_OTHER;
    gate->lengthSize = 0;
    gate->waitMode = WAIT_NONE;
    gate->dropped = 0;
    gate->entryPts = GST_CLOCK_TIME_NONE;
    gate->entryToleranceNs = 0;
    GstCaps *caps = gst_pad_get_current_caps( pad );
    if ( caps != nullptr )
    {
        configureGate( gate, caps );
        gst_caps_unref( caps );
    }
    gst_pad_add_probe( pad, static_cast<GstPadProbeType>(
                                GST_PAD_PROBE_TYPE_BUFFER
                                | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM
                                | GST_PAD_PROBE_TYPE_EVENT_FLUSH ),
                       &VideoPlayer::onDecoderInput, gate,
                       &VideoPlayer::freeDecoderGate );
    // The counter is added after the gate, so it only sees the buffers
    // that really reach the decoder. The difference between the number
    // of buffers before and after the gate is the work the gate saved.
    gst_pad_add_probe( pad, GST_PAD_PROBE_TYPE_BUFFER,
                       &VideoPlayer::onDecoderBuffer, _data, nullptr );
    gst_object_unref( pad );
}

GstPadProbeReturn VideoPlayer::onDecoderBuffer( GstPad *_pad,
                                                 GstPadProbeInfo *_info,
                                                 gpointer _data )
{
    Q_UNUSED( _pad );
    Q_UNUSED( _info );
    VideoPlayer *player = static_cast<VideoPlayer *>( _data );
    ++player->m_decoderBuffers;
    return GST_PAD_PROBE_OK;
}

GstPadProbeReturn VideoPlayer::onDecoderInput( GstPad *_pad,
                                               GstPadProbeInfo *_info,
                                               gpointer _data )
{
    Q_UNUSED( _pad );
    DecoderGate *gate = static_cast<DecoderGate *>( _data );

    if ( GST_PAD_PROBE_INFO_TYPE( _info )
         & ( GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM
             | GST_PAD_PROBE_TYPE_EVENT_FLUSH ) )
    {
        GstEvent *event = GST_PAD_PROBE_INFO_EVENT( _info );
        if ( event == nullptr )
        {
            return GST_PAD_PROBE_OK;
        }
        if ( GST_EVENT_TYPE( event ) == GST_EVENT_CAPS )
        {
            GstCaps *caps = nullptr;
            gst_event_parse_caps( event, &caps );
            configureGate( gate, caps );
        }
        else if ( GST_EVENT_TYPE( event ) == GST_EVENT_FLUSH_STOP )
        {
            // A keyframe seek in a container without an index (MPEG-TS)
            // lands anywhere in a GOP. For a preview, wait for a real IDR
            // so that a single picture is decoded; for an accurate seek
            // the demuxer starts at a keyframe, skip delta frames only.
            bool preview = gate->player->m_previewSeek;
            gate->dropped = 0;
            gate->waitMode = preview ? WAIT_IDR : WAIT_NON_DELTA;

            // The keyframe index knows the exact picture the decoder has
            // to start with. When it does, the gate drops every picture
            // before that one, so the decoder does not waste time on
            // pictures that the seek throws away anyway.
            guint64 entryPts = gate->player->m_entryPts;
            if ( !preview && GST_CLOCK_TIME_IS_VALID( entryPts ) )
            {
                gate->waitMode = WAIT_UNTIL_ENTRY;
                gate->entryPts = entryPts;
                gate->entryToleranceNs = gate->player->m_entryToleranceNs;
            }
            else
            {
                gate->entryPts = GST_CLOCK_TIME_NONE;
                gate->entryToleranceNs = 0;
            }

            // A preview decodes a single IDR and must never show a picture
            // with missing references: grey, or an old frame whose bar
            // reads as a valid but wrong id. An accurate seek must land on
            // the requested frame even if the demuxer started inside a
            // picture, so pictures with minor damage are shown there.
            // FLUSH_STOP is handled under the pad's stream lock, so the
            // streaming thread sees the new value for the next buffer.
            if ( gate->hasOutputCorrupt )
            {
                g_object_set( gate->decoder, "output-corrupt",
                              preview ? FALSE : TRUE, nullptr );
            }
        }
        return GST_PAD_PROBE_OK;
    }

    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER( _info );
    int mode = gate->waitMode;
    if ( buffer == nullptr || mode == WAIT_NONE )
    {
        return GST_PAD_PROBE_OK;
    }
    if ( ++gate->dropped >= MAX_GATE_DROPS )
    {
        gate->waitMode = WAIT_NONE;     // give up, never block playback
        return GST_PAD_PROBE_OK;
    }

    int codec = gate->codec;
    int type = ( codec != NalParser::CODEC_OTHER )
               ? NalParser::pictureNalType( codec, gate->lengthSize, buffer )
               : -1;
    bool h265 = ( codec == NalParser::CODEC_H265 );
    // H.264: 5 = IDR. H.265: 16-21 = IRAP (BLA, IDR, CRA), 8-9 = RASL
    bool irap = NalParser::isEntryPoint( codec, type );
    bool rasl = NalParser::isRasl( codec, type );

    if ( mode == WAIT_UNTIL_ENTRY )
    {
        // The index says where the decoder has to start. Every picture
        // before that one is dropped here, so it never reaches the
        // decoder and never costs any decoding time.
        //
        // The comparison uses the raw timestamp of the picture, because
        // that is the one value the scan and the decoder are guaranteed
        // to agree on: the demuxer keeps the timestamps of the file and
        // only moves the origin of the segment.
        guint64 entryPts = gate->entryPts;
        gint64 tolerance = gate->entryToleranceNs;
        if ( GST_CLOCK_TIME_IS_VALID( entryPts )
             && GST_BUFFER_PTS_IS_VALID( buffer ) )
        {
            gint64 pts = static_cast<gint64>( GST_BUFFER_PTS( buffer ) );
            gint64 target = static_cast<gint64>( entryPts );
            if ( pts + tolerance < target )
            {
                return GST_PAD_PROBE_DROP;
            }
        }
        // The entry point has been reached: from here on every picture
        // belongs to the group of pictures the seek asked for.
        gate->waitMode = WAIT_NONE;
        return GST_PAD_PROBE_OK;
    }

    if ( mode == WAIT_SKIP_RASL )
    {
        // RASL pictures reference the GOP before the CRA we started from:
        // they cannot be decoded after a seek and must be skipped.
        if ( rasl )
        {
            return GST_PAD_PROBE_DROP;
        }
        gate->waitMode = WAIT_NONE;
        return GST_PAD_PROBE_OK;
    }

    // - H.265: the NAL type decides. Parsers mark the first buffer after a
    //   discontinuity as a keyframe even when it is not one, and the libav
    //   HEVC decoder does not flag the broken pictures that follow.
    // - H.264 preview: only an IDR, so that one picture is decoded.
    // - H.264 accurate seek: the demuxer may start at a recovery point,
    //   which is a valid entry point; pictures that are not yet refreshed
    //   are flagged corrupt by libav and hidden (output-corrupt = false).
    // - Other codecs can only be judged by the DELTA_UNIT flag.
    bool useNalType = h265 || ( codec == NalParser::CODEC_H264
                                && mode == WAIT_IDR );
    bool clean = useNalType
                 ? irap
                 : !GST_BUFFER_FLAG_IS_SET( buffer,
                                            GST_BUFFER_FLAG_DELTA_UNIT );
    if ( !clean )
    {
        return GST_PAD_PROBE_DROP;
    }
    gate->waitMode = h265 ? WAIT_SKIP_RASL : WAIT_NONE;
    return GST_PAD_PROBE_OK;
}

void VideoPlayer::freeDecoderGate( gpointer _data )
{
    delete static_cast<DecoderGate *>( _data );
}

void VideoPlayer::configureGate( DecoderGate *_gate, GstCaps *_caps )
{
    // The parser recognises the codec and the way the NAL units are
    // separated, so the gate and the keyframe index always agree.
    int codec = NalParser::codecFromCaps( _caps );
    _gate->codec = codec;
    _gate->lengthSize = NalParser::lengthSizeFromCaps( _caps );

    // The name of the codec is shown in the overlay. The gate lives on a
    // streaming thread, so the name is written to an atomic string
    // holder and read by the GUI thread.
    QString name = QString::fromLatin1( NalParser::codecName( codec ) );
    QMutexLocker locker( &m_codecMutex );
    if ( m_videoCodec != name )
    {
        m_videoCodec = name;
        // The GUI thread is told through a queued call, because the
        // streaming thread must not touch the properties of the player.
        QMetaObject::invokeMethod(
            this, [this]() { emit videoInfoChanged(); },
            Qt::QueuedConnection );
    }
}
