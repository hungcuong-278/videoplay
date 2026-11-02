#ifndef VIDEOPLAYER_HPP
#define VIDEOPLAYER_HPP

#include <QObject>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QTimer>
#include <QUrl>
#include <QString>
#include <QDebug>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QVariantMap>

#include <cstring>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <gst/video/gstvideodecoder.h>

#include <atomic>

#include "barcodeReader.hpp"
#include "nalParser.hpp"
#include "keyframeIndex.hpp"

/*!
 * \brief Plays any video supported by the installed GStreamer plugins and
 *        delivers the decoded frames to the GUI thread.
 *
 * Threading model:
 * - GStreamer decodes on its own streaming threads. The appsink callbacks
 *   run on one of them and must never touch QML objects.
 * - The newest frame is handed over through a single "latest frame" slot
 *   guarded by m_frameMutex. An older frame that the GUI has not taken yet
 *   is overwritten, so a slow GUI can never make a queue grow without bound.
 * - Bus messages are forwarded to the GUI thread with a queued call and
 *   tagged with a pipeline generation, so messages of a closed pipeline
 *   are ignored.
 */
class VideoPlayer : public QObject
{
    Q_OBJECT
    Q_PROPERTY( bool playing READ isPlaying NOTIFY playingChanged )
    Q_PROPERTY( qint64 position READ position NOTIFY positionChanged )
    Q_PROPERTY( qint64 duration READ duration NOTIFY durationChanged )
    Q_PROPERTY( QString source READ source NOTIFY sourceChanged )
    Q_PROPERTY( QString errorString READ errorString NOTIFY errorChanged )
    Q_PROPERTY( quint64 deliveredFrames READ deliveredFrames
                NOTIFY frameReady )
    Q_PROPERTY( qint64 frameId READ frameId NOTIFY frameReady )
    Q_PROPERTY( qint64 frameTime READ frameTime NOTIFY frameReady )
    Q_PROPERTY( double rate READ rate NOTIFY rateChanged )
    Q_PROPERTY( bool seekable READ isSeekable NOTIFY seekableChanged )
    Q_PROPERTY( bool useKeyframeIndex READ useKeyframeIndex
                WRITE setUseKeyframeIndex NOTIFY useKeyframeIndexChanged )
    Q_PROPERTY( quint64 unreadableFrames READ unreadableFrames
                NOTIFY frameReady )
    Q_PROPERTY( quint64 decoderBuffers READ decoderBuffers
                NOTIFY decoderBuffersChanged )
    Q_PROPERTY( QVariantMap videoInfo READ videoInfo NOTIFY videoInfoChanged )

public:
    /*!
     * \brief Creates an idle player without a pipeline.
     * \param _parent QObject parent.
     */
    explicit VideoPlayer( QObject *_parent = nullptr );

    /*!
     * \brief Stops the streaming threads before the object is destroyed.
     */
    ~VideoPlayer() override;

    /*!
     * \brief Opens a video and prerolls it, showing the first frame paused.
     * \param _url Local file URL or any URI GStreamer understands.
     * \return True if the pipeline was created.
     */
    Q_INVOKABLE void open( const QUrl &_url );

    /*!
     * \brief Gives the player a keyframe index to seek faster.
     *
     * The index is not owned by the player. With an index, an accurate
     * seek drops every picture before the last keyframe of the target,
     * so the decoder does not decode pictures that would be thrown away.
     * \param _index Keyframe index, may be nullptr.
     */
    void setKeyframeIndex( KeyframeIndex *_index );

    /*!
     * \brief Returns the keyframe index of the player.
     */
    KeyframeIndex *keyframeIndex() const;

    /*!
     * \brief Turns the use of the keyframe index on or off.
     *
     * The index makes the accurate seeks of a file without a keyframe
     * table faster. It can be switched off to compare the behaviour.
     * \param _enabled True to use the index.
     */
    void setUseKeyframeIndex( bool _enabled );

    /*!
     * \brief Returns true when the keyframe index is used.
     */
    bool useKeyframeIndex() const;

    /*!
     * \brief Starts or resumes playback.
     */
    Q_INVOKABLE void play();

    /*!
     * \brief Pauses playback and keeps the current frame on screen.
     */
    Q_INVOKABLE void pause();

    /*!
     * \brief Switches between play and pause.
     */
    Q_INVOKABLE void togglePlay();

    /*!
     * \brief Jumps to a position. Playback continues if it was playing.
     *
     * Only one seek runs at a time. Requests made meanwhile replace each
     * other, and the newest one is started when the running seek is done,
     * so dragging the slider never builds up a backlog.
     * \param _positionMs Target on the stream timeline, in milliseconds.
     * \param _accurate True to land on the exact frame (decodes from the
     *        previous keyframe), false to jump to the nearest keyframe.
     */
    Q_INVOKABLE void seek( qint64 _positionMs, bool _accurate = true );

    /*!
     * \brief Jumps forward or backward from the frame on screen.
     * \param _deltaMs Offset in milliseconds, negative to go back.
     */
    Q_INVOKABLE void seekRelative( qint64 _deltaMs );

    /*!
     * \brief Pauses and shows the next frame.
     */
    Q_INVOKABLE void stepForward();

    /*!
     * \brief Pauses and shows the previous frame.
     */
    Q_INVOKABLE void stepBackward();

    /*!
     * \brief Changes the playback speed.
     * \param _rate Speed factor, from MIN_RATE to MAX_RATE.
     */
    Q_INVOKABLE void setRate( double _rate );

    /*!
     * \brief Returns true while the pipeline is in the PLAYING state.
     */
    bool isPlaying() const;

    /*!
     * \brief Returns the playback position in milliseconds.
     */
    qint64 position() const;

    /*!
     * \brief Returns the stream duration in milliseconds, or 0 if unknown.
     */
    qint64 duration() const;

    /*!
     * \brief Returns the opened URI.
     */
    QString source() const;

    /*!
     * \brief Returns the last error message, empty when there is none.
     */
    QString errorString() const;

    /*!
     * \brief Returns the frame to display. GUI thread only.
     */
    QImage currentFrame() const;

    /*!
     * \brief Returns how many frames have reached the GUI thread.
     */
    quint64 deliveredFrames() const;

    /*!
     * \brief Returns the id read from the bar of the current frame,
     *        or -1 when the frame has no readable bar.
     */
    qint64 frameId() const;

    /*!
     * \brief Returns the time of the displayed frame in milliseconds,
     *        taken from its own timestamp, or -1 if unknown.
     */
    qint64 frameTime() const;

    /*!
     * \brief Returns the playback speed factor.
     */
    double rate() const;

    /*!
     * \brief Returns true if the opened stream supports seeking.
     */
    bool isSeekable() const;

    /*!
     * \brief Returns how many delivered frames had no readable bar.
     */
    quint64 unreadableFrames() const;

    /*!
     * \brief Returns how many buffers reached the video decoder.
     *
     * The number shows the cost of a seek: a container without a
     * keyframe table makes the decoder decode hundreds of pictures that
     * are thrown away again.
     */
    quint64 decoderBuffers() const;

    /*!
     * \brief Returns the properties of the opened video.
     *
     * The map holds "codec", "width", "height" and "fps", as far as
     * they are known.
     */
    QVariantMap videoInfo() const;

signals:
    void playingChanged();
    void positionChanged();
    void durationChanged();
    void sourceChanged();
    void errorChanged();
    void frameReady();
    void rateChanged();
    void seekableChanged();
    void useKeyframeIndexChanged();
    void decoderBuffersChanged();
    void videoInfoChanged();

private:
    /*!
     * \brief Per-pipeline data passed to GStreamer callbacks.
     *
     * Callbacks only read this struct, which never changes after creation,
     * so no lock is needed to access it from a streaming thread.
     */
    struct CallbackContext
    {
        VideoPlayer *player;
        quint64 generation;
    };

    void closePipeline();
    void deliverFrame();
    void handleBusMessage( quint64 _generation, GstMessage *_message );
    void updatePosition();
    void updateDuration();
    void setError( const QString &_text );
    void storeFrame( GstSample *_sample );
    void startSeek( qint64 _positionMs, bool _accurate );
    void updateEntryPoint( qint64 _positionMs, bool _accurate );
    void updateNominalFrameDuration();
    void updateVideoInfo();
    void onSeekDone();
    void updateSeekable();

    static QImage sampleToImage( GstSample *_sample );
    static GstFlowReturn onNewSample( GstAppSink *_sink, gpointer _data );
    static GstFlowReturn onNewPreroll( GstAppSink *_sink, gpointer _data );
    static GstBusSyncReply onBusSync( GstBus *_bus, GstMessage *_message,
                                      gpointer _data );
    static GstPadProbeReturn onFlush( GstPad *_pad, GstPadProbeInfo *_info,
                                      gpointer _data );
    static void freeContext( gpointer _data );

    /*!
     * \brief Keyframe gate in front of one video decoder.
     *
     * Configured on CAPS and FLUSH_STOP events and used for buffers. Events
     * and buffers of a pad are serialized by GStreamer, but FLUSH_STOP
     * comes from the seeking thread, hence the atomics.
     */
    struct DecoderGate
    {
        VideoPlayer *player;
        GstElement *decoder;            // owns the pad, outlives the gate
        bool hasOutputCorrupt;
        std::atomic<int> codec;         // CodecKind
        std::atomic<int> lengthSize;    // NAL length prefix, 0 = start codes
        std::atomic<int> waitMode;      // WaitMode
        std::atomic<int> dropped;
        // The picture an accurate seek has to start at, and how far the
        // index and the decoder may disagree about its time.
        std::atomic<guint64> entryPts;
        std::atomic<gint64> entryToleranceNs;
    };

    enum WaitMode
    {
        WAIT_NONE = 0,
        WAIT_NON_DELTA = 1,     // accurate seek: wait for an entry point
        WAIT_IDR = 2,           // preview seek: wait for an IDR / IRAP
        WAIT_SKIP_RASL = 3,     // H.265 after an IRAP: skip RASL pictures
        WAIT_UNTIL_ENTRY = 4    // accurate seek: wait for a known keyframe
    };

    static void onDeepElementAdded( GstBin *_bin, GstBin *_subBin,
                                    GstElement *_element, gpointer _data );
    static GstPadProbeReturn onDecoderInput( GstPad *_pad,
                                             GstPadProbeInfo *_info,
                                             gpointer _data );
    static void freeDecoderGate( gpointer _data );
    static GstPadProbeReturn onDecoderBuffer( GstPad *_pad,
                                              GstPadProbeInfo *_info,
                                              gpointer _data );
    static void configureGate( DecoderGate *_gate, GstCaps *_caps );

    // Give up waiting after this many dropped buffers: a stream that
    // never marks keyframes must still play.
    static const int MAX_GATE_DROPS = 600;
    // An accurate seek that lands more than this many frames after its
    // target has failed (the demuxer sometimes starts inside a keyframe)
    static const int MAX_LANDING_FRAMES = 3;
    static const int MAX_ACCURATE_RETRIES = 2;

    static const int POSITION_INTERVAL_MS = 100;
    // An accurate seek in an MPEG-TS file with 10 s GOPs can take > 3 s
    static const int SEEK_TIMEOUT_MS = 10000;
    static const double MIN_RATE;
    static const double MAX_RATE;
    static const int MAX_STEP_BACK_TRIES = 6;
    static const qint64 DEFAULT_FRAME_MS = 40;

    // GUI thread only
    GstElement *m_pipeline;
    GstElement *m_appSink;
    quint64 m_generation;
    QTimer m_positionTimer;
    bool m_playing;
    qint64 m_position;
    qint64 m_duration;
    QString m_source;
    QString m_errorString;
    double m_rate;
    bool m_seekable;
    bool m_atEnd;
    bool m_seekInFlight;
    QElapsedTimer m_seekClock;
    qint64 m_pendingSeekMs;         // newest request while one runs, or -1
    bool m_pendingSeekAccurate;
    qint64 m_stepBackFromTime;      // frame a backward step started from
    qint64 m_stepBackFromId;
    int m_stepBackTries;            // -1 when no backward step is running
    qint64 m_nominalFrameMs;        // from the frame rate in the caps
    QImage m_currentFrame;
    qint64 m_currentFrameId;
    qint64 m_currentFrameTime;
    qint64 m_currentFrameEnd;
    qint64 m_latestFrameEnd;
    quint64 m_deliveredFrames;
    quint64 m_unreadableFrames;

    // Shared with the streaming thread, guarded by m_frameMutex.
    // A frame and its id are always written and read together, so the
    // GUI can never show the picture of one frame with the id of another.
    QMutex m_frameMutex;
    QImage m_pendingFrame;
    qint64 m_pendingFrameId;
    qint64 m_pendingFrameTime;
    qint64 m_pendingFrameEnd;
    bool m_hasPendingFrame;
    GstClockTime m_lastStoredPts;
    // Incremented at every flush (seek). A frame whose processing started
    // before the flush carries the old value and is discarded.
    quint64 m_flushGeneration;

    // Set by the GUI thread before a seek, read by the decoder gates
    std::atomic<bool> m_previewSeek;
    bool m_previewInFlight;     // GUI thread only
    qint64 m_accurateTarget;    // target of the running accurate seek
    int m_accurateRetries;

    // The time of the keyframe an accurate seek has to start at. The
    // gate drops every picture before it, so the decoder does not waste
    // time on pictures that the seek would throw away anyway.
    // GST_CLOCK_TIME_NONE means there is no known entry point.
    std::atomic<guint64> m_entryPts;
    // How far the index and the player may disagree on the time of a
    // keyframe, in nanoseconds. A small value keeps the gate from
    // waiting for a keyframe that has already passed.
    std::atomic<gint64> m_entryToleranceNs;

    // The keyframe index of the opened file, not owned. It is only read
    // from the GUI thread, so no lock is needed.
    KeyframeIndex *m_keyframeIndex;
    // Whether the index is used for the seeks. It can be switched off to
    // compare the behaviour of the player with and without it.
    bool m_useKeyframeIndex;
    // Number of buffers that reached the video decoder. It shows the
    // cost of a seek. It is written by the streaming threads, so it is
    // atomic.
    std::atomic<quint64> m_decoderBuffers;
    // The properties of the opened video, shown in the overlay.
    QVariantMap m_videoInfo;
    // The name of the codec, written by the streaming threads that
    // negotiate the caps, read by the GUI thread.
    QString m_videoCodec;
    mutable QMutex m_codecMutex;
};

#endif // VIDEOPLAYER_HPP
