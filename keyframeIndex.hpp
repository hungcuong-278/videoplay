#ifndef KEYFRAMEINDEX_HPP
#define KEYFRAMEINDEX_HPP

#include <QObject>
#include <QAtomicInt>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMutex>
#include <QSharedPointer>
#include <QString>
#include <QVector>

#include <QtConcurrent/QtConcurrentRun>

#include <functional>

#include "nalParser.hpp"

/*!
 * \brief Lists the timestamps of the keyframes of a video file.
 *
 * A container with an index (MP4, Matroska) knows where every keyframe
 * is, so seeking there costs the length of one group of pictures. A
 * container without an index (MPEG-TS, raw streams) has no such table:
 * the demuxer has to guess a byte position, step back far enough to be
 * sure, and then look for the first keyframe. The guess is often several
 * groups of pictures too early, and the decoder then decodes hundreds of
 * pictures that are thrown away again.
 *
 * This class builds the missing table: it walks through the file once,
 * without decoding it, and remembers the time of every keyframe. A seek
 * can then start exactly where the table says.
 *
 * The table is usable while the scan is still running: the part that has
 * been scanned is published after every keyframe, so a seek into the
 * already scanned part is already fast.
 *
 * Two timelines are stored for every keyframe:
 * - The time on the stream timeline, in milliseconds. It is the time the
 *   player shows, so it is the one a seek target is compared with.
 * - The raw timestamp of the picture, in nanoseconds. The demuxer keeps
 *   the timestamps of the file and only moves the origin of the segment
 *   (an MPEG-TS file starts at 3600 s but is shown from 125 ms), so the
 *   raw timestamp is the only value that the decoder gate and the scan
 *   are guaranteed to agree on.
 *
 * Threading model:
 * - The scan runs on a thread of the global QThreadPool. It owns its own
 *   GStreamer pipeline and shares nothing with the GUI thread.
 * - The result is published as an immutable snapshot behind a short
 *   mutex, so any thread may read the keyframe times without locking
 *   for long.
 * - A shared atomic flag cancels a running scan, and a generation number
 *   discards a result that arrives after the file has changed.
 */
class KeyframeIndex : public QObject
{
    Q_OBJECT
    Q_PROPERTY( bool scanning READ isScanning NOTIFY scanningChanged )
    Q_PROPERTY( int progress READ progress NOTIFY progressChanged )
    Q_PROPERTY( int keyframeCount READ keyframeCount
                NOTIFY keyframeChanged )
    Q_PROPERTY( bool complete READ isComplete NOTIFY keyframeChanged )
    Q_PROPERTY( qint64 durationMs READ durationMs NOTIFY keyframeChanged )
    Q_PROPERTY( qint64 largestGapMs READ largestGapMs
                NOTIFY keyframeChanged )
    Q_PROPERTY( qint64 lastKnownTimeMs READ lastKnownTimeMs
                NOTIFY keyframeChanged )
    Q_PROPERTY( QString path READ path NOTIFY scanningChanged )
    Q_PROPERTY( QString errorString READ errorString NOTIFY scanningChanged )

public:
    /*!
     * \brief Creates an empty index.
     * \param _parent QObject parent.
     */
    explicit KeyframeIndex( QObject *_parent = nullptr );

    /*!
     * \brief Cancels a running scan and waits for it to stop.
     */
    ~KeyframeIndex() override;

    /*!
     * \brief Starts scanning a video file for keyframes.
     *
     * A cache file written by an earlier run is used when it still
     * matches the video file, so the second start is immediate.
     * \param _filePath Path of the video file.
     */
    void start( const QString &_filePath );

    /*!
     * \brief Stops a running scan.
     */
    void stop();

    /*!
     * \brief Returns true while a scan is running.
     */
    bool isScanning() const;

    /*!
     * \brief Returns how much of the file has been scanned, 0 to 100.
     */
    int progress() const;

    /*!
     * \brief Returns the number of keyframes found so far.
     */
    int keyframeCount() const;

    /*!
     * \brief Returns true when the whole file has been scanned.
     */
    bool isComplete() const;

    /*!
     * \brief Returns the exact duration of the file in milliseconds.
     *
     * A container without an index only reports an estimated duration,
     * which is often too short. The scan knows the time of the last
     * picture of the file, so it knows the real duration.
     * \return Duration in milliseconds, or 0 when it is not known yet.
     */
    qint64 durationMs() const;

    /*!
     * \brief Returns the largest gap between two keyframes, in milliseconds.
     *
     * The gap tells how many pictures a seek has to decode at the most.
     * \return Gap in milliseconds, or 0 when it is not known yet.
     */
    qint64 largestGapMs() const;

    /*!
     * \brief Returns the time of the last keyframe found, in milliseconds.
     *
     * The value tells how far the scan has reached. A seek target
     * before that time can be served from the table, a later one cannot.
     * \return Time in milliseconds, or -1 when nothing was found yet.
     */
    qint64 lastKnownTimeMs() const;

    /*!
     * \brief Returns the path of the file being scanned.
     */
    QString path() const;

    /*!
     * \brief Returns the last error message, empty when there is none.
     */
    QString errorString() const;

    /*!
     * \brief Returns the time of the last keyframe at or before a moment.
     * \param _timeMs Position on the stream timeline, in milliseconds.
     * \return Time of the keyframe in milliseconds, or -1 when there is
     *         no keyframe before that moment.
     */
    qint64 entryTimeBefore( qint64 _timeMs ) const;

    /*!
     * \brief Looks up the entry point of a seek.
     *
     * The entry point is the last keyframe at or before the target. The
     * pictures before it would be decoded and thrown away again, so a
     * decoder gate can skip them.
     * \param _timeMs Target of the seek, on the stream timeline.
     * \param ptsNs_ Receives the raw timestamp of the keyframe.
     * \return True when a usable entry point is known.
     */
    bool entryPtsBefore( qint64 _timeMs, qint64 &ptsNs_ ) const;

    /*!
     * \brief Returns the times of all keyframes found, in milliseconds.
     */
    QVector<qint64> keyframeTimes() const;

signals:
    void scanningChanged();
    void progressChanged();
    void keyframeChanged();

private:
    /*!
     * \brief Immutable snapshot of what the scan has found.
     */
    struct Snapshot
    {
        QVector<qint64> times;     // keyframe times, in milliseconds
        QVector<qint64> pts;       // raw timestamps, in nanoseconds
        bool complete = false;
        QString errorString;
        qint64 durationMs = 0;     // exact duration, 0 when unknown
        qint64 largestGapMs = 0;    // largest gap between two keyframes
    };

    typedef QSharedPointer<const Snapshot> SnapshotPointer;

    /*!
     * \brief What the scan reports to the GUI thread.
     */
    struct ScanResult
    {
        QVector<qint64> times;
        QVector<qint64> pts;
        int percent = 0;
        bool complete = false;
        qint64 durationMs = 0;
        qint64 largestGapMs = 0;
    };

    /*!
     * \brief Called by the scan when it has new results.
     *
     * The callback runs on the pool thread.
     */
    typedef std::function< void( const ScanResult & ) > ProgressCallback;

    /*!
     * \brief Walks through a file and collects the time of every keyframe.
     *
     * Runs on a pool thread. It only reads the file and reports what it
     * has found, so it shares nothing mutable with the GUI thread.
     * \param _filePath Path of the video file.
     * \param _cancel Set to 1 by the GUI thread to stop early.
     * \param _progress Called when the scan has new results.
     * \return The keyframes found, or a null pointer when cancelled.
     */
    static SnapshotPointer scanFile( const QString &_filePath,
                                     QSharedPointer<QAtomicInt> _cancel,
                                     const ProgressCallback &_progress );

    /*!
     * \brief Publishes a new snapshot to the readers.
     * \param _snapshot New snapshot, may be null to drop the index.
     */
    void publish( const SnapshotPointer &_snapshot );

    /*!
     * \brief Writes the keyframe times next to the video file.
     * \param _filePath Path of the video file.
     * \param _times Keyframe times, in milliseconds.
     * \param _pts Raw timestamps of the keyframes, in nanoseconds.
     * \param _durationMs Exact duration of the file, in milliseconds.
     * \return True when the cache file was written.
     */
    static bool writeCache( const QString &_filePath,
                            const QVector<qint64> &_times,
                            const QVector<qint64> &_pts,
                            qint64 _durationMs );

    /*!
     * \brief Reads the keyframe times of a cache file.
     * \param _filePath Path of the video file.
     * \param times_ Receives the keyframe times, in milliseconds.
     * \param pts_ Receives the raw timestamps, in nanoseconds.
     * \param durationMs_ Receives the exact duration, in milliseconds.
     * \return True when a matching cache file was found and read.
     */
    static bool readCache( const QString &_filePath,
                           QVector<qint64> &times_,
                           QVector<qint64> &pts_,
                           qint64 &durationMs_ );

    /*!
     * \brief Returns the path of the cache file of a video file.
     * \param _filePath Path of the video file.
     */
    static QString cachePath( const QString &_filePath );

    /*!
     * \brief Returns the largest gap between two neighbouring keyframes.
     * \param _times Keyframe times, in milliseconds, sorted.
     * \return Gap in milliseconds, 0 when there is less than two.
     */
    static qint64 largestGap( const QVector<qint64> &_times );

    void onScanFinished();
    void setError( const QString &_text );

    mutable QMutex m_mutex;
    SnapshotPointer m_snapshot;
    QString m_path;
    QString m_errorString;
    int m_progress;
    bool m_scanning;
    QSharedPointer<QAtomicInt> m_cancelFlag;
    QFutureWatcher<SnapshotPointer> m_watcher;
    quint64 m_generation;
    quint64 m_watchedGeneration;
};

#endif // KEYFRAMEINDEX_HPP
