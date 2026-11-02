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
     * \brief Returns the times of all keyframes found, in milliseconds.
     */
    QVector<qint64> keyframeTimes() const;

signals:
    void scanningChanged();
    void progressChanged();
    void keyframeChanged();

private:
    /*!
     * \brief Immutable snapshot of the keyframes found so far.
     */
    struct Snapshot
    {
        QVector<qint64> times;     // keyframe times, in milliseconds
        bool complete = false;
        QString errorString;
    };

    typedef QSharedPointer<const Snapshot> SnapshotPointer;

    /*!
     * \brief Called by the scan when it has found something new.
     *
     * The arguments hold the keyframes found so far, how much of the
     * file has been read in percent, and whether the whole file has been
     * scanned.
     */
    typedef std::function< void( const QVector<qint64> &, int, bool ) >
            ProgressCallback;

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
     * \return True when the cache file was written.
     */
    static bool writeCache( const QString &_filePath,
                            const QVector<qint64> &_times );

    /*!
     * \brief Reads the keyframe times of a cache file.
     * \param _filePath Path of the video file.
     * \param times_ Receives the keyframe times, in milliseconds.
     * \return True when a matching cache file was found and read.
     */
    static bool readCache( const QString &_filePath,
                           QVector<qint64> &times_ );

    /*!
     * \brief Returns the path of the cache file of a video file.
     * \param _filePath Path of the video file.
     */
    static QString cachePath( const QString &_filePath );

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
