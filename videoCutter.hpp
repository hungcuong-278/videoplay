#ifndef VIDEOCUTTER_HPP
#define VIDEOCUTTER_HPP

#include <QObject>
#include <QFutureWatcher>
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <memory>

#include "barcodeReader.hpp"

/*!
 * \brief Cuts a part out of a video without changing the pictures.
 *
 * A cut of a video file is normally done by copying the stream into a
 * new file. When the new file is encoded again, the pictures change,
 * and a barcode that was read from the original cannot be found in the
 * new file anymore. Copying the stream without encoding keeps every
 * picture bit for bit, so the barcode of a frame stays the same.
 *
 * The problem is the start of the cut: a stream can only start at a
 * keyframe, so the copy has to begin at the keyframe before the marked
 * frame. The part before the mark is therefore in the new file too.
 *
 * To find out which pictures really belong to the cut, the cutter reads
 * the barcode of every picture of the new file and uses the ids it
 * finds to cut the rows of the metadata table. The rows are then
 * exactly the rows of the marked frames, with no guessing.
 *
 * The work runs on a thread of the global QThreadPool, so the interface
 * stays responsive. The progress is reported through signals.
 */
class VideoCutter : public QObject
{
    Q_OBJECT
    Q_PROPERTY( bool running READ isRunning NOTIFY runningChanged )
    Q_PROPERTY( int progress READ progress NOTIFY progressChanged )
    Q_PROPERTY( QString statusText READ statusText NOTIFY statusChanged )
    Q_PROPERTY( QString resultPath READ resultPath NOTIFY finished )
    Q_PROPERTY( QString errorString READ errorString NOTIFY failed )
    Q_PROPERTY( int framesVerified READ framesVerified
                NOTIFY progressChanged )
    Q_PROPERTY( QStringList keptIds READ keptIds NOTIFY finished )
    Q_PROPERTY( qint64 cutStartMs READ cutStartMs NOTIFY finished )
    Q_PROPERTY( qint64 cutEndMs READ cutEndMs NOTIFY finished )

public:
    /*!
     * \brief Creates a cutter that has not run yet.
     * \param _parent QObject parent.
     */
    explicit VideoCutter( QObject *_parent = nullptr );

    /*!
     * \brief Stops a running cut.
     */
    ~VideoCutter() override;

    /*!
     * \brief Cuts a part out of a video file.
     *
     * The part is copied without encoding, so the pictures stay the
     * same. The copy starts at the keyframe before the marked frame, so
     * the new file can be played at all.
     * \param _videoPath Path of the video file.
     * \param _targetPath Path of the new file, without an extension.
     * \param _startMs Start of the cut on the stream timeline.
     * \param _endMs End of the cut on the stream timeline.
     * \param _startId First id that belongs to the cut.
     * \param _endId Last id that belongs to the cut.
     */
    void start( const QString &_videoPath, const QString &_targetPath,
                qint64 _startMs, qint64 _endMs,
                qint64 _startId, qint64 _endId );

    /*!
     * \brief Returns true while the cut is running.
     */
    bool isRunning() const;

    /*!
     * \brief Returns how much of the work is done, 0 to 100.
     */
    int progress() const;

    /*!
     * \brief Returns a short description of the current step.
     */
    QString statusText() const;

    /*!
     * \brief Returns the path of the file that was written.
     */
    QString resultPath() const;

    /*!
     * \brief Returns the last error message, empty when there is none.
     */
    QString errorString() const;

    /*!
     * \brief Returns how many pictures of the new file were checked.
     */
    int framesVerified() const;

    /*!
     * \brief Returns the ids that belong to the cut.
     *
     * The ids come from the barcode of the pictures of the new file, so
     * they are the real ids and not the ones that were asked for.
     */
    QStringList keptIds() const;

    /*!
     * \brief Returns the time the cut really starts at.
     *
     * This is the time of the keyframe the copy started at, which can
     * be a little before the marked frame.
     */
    qint64 cutStartMs() const;

    /*!
     * \brief Returns the time the cut really ends at.
     */
    qint64 cutEndMs() const;

signals:
    void runningChanged();
    void progressChanged();
    void statusChanged();
    void finished();
    void failed();

    /*!
     * \brief Cancels a running cut.
     */
    typedef std::shared_ptr<std::atomic<bool> > CancelFlag;

private:
    /*!
     * \brief What the cut reports to the GUI thread.
     */
    struct CutResult
    {
        bool ok = false;
        QString errorString;
        QString resultPath;
        QStringList keptIds;
        qint64 cutStartMs = 0;
        qint64 cutEndMs = 0;
        int framesVerified = 0;
    };

    /*!
     * \brief Cuts a part out of a file and checks what is in it.
     *
     * Runs on a pool thread. The stream is copied into a new file, then
     * the barcode of every picture of the new file is read, so the ids
     * of the pictures that belong to the cut are known exactly.
     * \param _videoPath Path of the video file.
     * \param _targetPath Path of the new file, without an extension.
     * \param _startMs Start of the cut.
     * \param _endMs End of the cut.
     * \param _startId First id that belongs to the cut.
     * \param _endId Last id that belongs to the cut.
     * \param _cancel Set to true by the GUI thread to stop early.
     * \param _progress Called with the progress and the current step.
     * \return The result of the cut.
     */
    static CutResult runCut( const QString &_videoPath,
                              const QString &_targetPath,
                              qint64 _startMs, qint64 _endMs,
                              qint64 _startId, qint64 _endId,
                              const CancelFlag &_cancel,
                              const std::function< void( int,
                                        const QString & ) > &_progress );

    /*!
     * \brief Copies a part of a file into a new file, without encoding.
     *
     * Runs on a pool thread.
     * \param _videoPath Path of the video file.
     * \param _targetPath Path of the new file.
     * \param _startMs Start of the cut.
     * \param _endMs End of the cut.
     * \param _cancel Set to true by the GUI thread to stop early.
     * \param _progress Called with the progress and the current step.
     * \param cutStartMs_ Receives the time the copy really starts at.
     * \param cutEndMs_ Receives the time the copy really ends at.
     * \return True when the copy worked.
     */
    static bool copyStream( const QString &_videoPath,
                             const QString &_targetPath,
                             qint64 _startMs, qint64 _endMs,
                             const CancelFlag &_cancel,
                             const std::function< void( int,
                                       const QString & ) > &_progress,
                             qint64 &cutStartMs_, qint64 &cutEndMs_,
                             QString &_errorString );

    /*!
     * \brief Reads the barcode of every picture of a file.
     *
     * Runs on a pool thread. Only the pictures are decoded, nothing is
     * written.
     * \param _path Path of the file.
     * \param _cancel Set to true by the GUI thread to stop early.
     * \param _progress Called with the number of pictures checked.
     * \param countOut_ Receives the number of pictures.
     * \return The ids that were found.
     */
    static QStringList readIds( const QString &_path,
                                const CancelFlag &_cancel,
                                const std::function< void( int ) > &_progress,
                                int &countOut_ );

    /*!
     * \brief Keeps only the ids of the cut, in the order of the file.
     * \param _ids Ids found in the new file.
     * \param _startId First id that belongs to the cut.
     * \param _endId Last id that belongs to the cut.
     */
    static QStringList filterIds( const QStringList &_ids,
                                   qint64 _startId, qint64 _endId );

    void onCutFinished();
    void setStatus( const QString &_text );
    void setError( const QString &_text );

    CancelFlag m_cancel;
    QFutureWatcher<CutResult> m_watcher;
    bool m_running;
    int m_progress;
    QString m_statusText;
    QString m_errorString;
    QString m_resultPath;
    QStringList m_keptIds;
    int m_framesVerified;
    qint64 m_cutStartMs;
    qint64 m_cutEndMs;
};

#endif // VIDEOCUTTER_HPP
