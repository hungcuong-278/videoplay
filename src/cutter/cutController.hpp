#ifndef CUTCONTROLLER_HPP
#define CUTCONTROLLER_HPP

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include "metadataStore.hpp"
#include "videoCutter.hpp"
#include "videoPlayer.hpp"

/*!
 * \brief Puts the cutting of a video and the metadata table together.
 *
 * The cutter needs three things: the video file, the range of the cut
 * and the ids of the frames that belong to it. This class holds the
 * marked range, starts the cutter and writes the metadata file of the
 * cut when the new video file is ready.
 *
 * The class lives in the GUI thread, so the marked range can be read by
 * the interface without a lock.
 */
class CutController : public QObject
{
    Q_OBJECT
    Q_PROPERTY( bool hasMarkIn READ hasMarkIn NOTIFY marksChanged )
    Q_PROPERTY( bool hasMarkOut READ hasMarkOut NOTIFY marksChanged )
    Q_PROPERTY( qint64 markInMs READ markInMs NOTIFY marksChanged )
    Q_PROPERTY( qint64 markOutMs READ markOutMs NOTIFY marksChanged )
    Q_PROPERTY( qint64 markInId READ markInId NOTIFY marksChanged )
    Q_PROPERTY( qint64 markOutId READ markOutId NOTIFY marksChanged )
    Q_PROPERTY( bool running READ isRunning NOTIFY runningChanged )
    Q_PROPERTY( int progress READ progress NOTIFY progressChanged )
    Q_PROPERTY( QString statusText READ statusText NOTIFY statusChanged )
    Q_PROPERTY( QString resultVideoPath READ resultVideoPath
                NOTIFY finished )
    Q_PROPERTY( QString resultCsvPath READ resultCsvPath NOTIFY finished )
    Q_PROPERTY( int keptFrames READ keptFrames NOTIFY finished )
    Q_PROPERTY( QString errorString READ errorString NOTIFY failed )

public:
    /*!
     * \brief Creates a controller that has not cut anything yet.
     * \param _parent QObject parent.
     */
    explicit CutController( QObject *_parent = nullptr );

    /*!
     * \brief Marks the frame on screen as the start of the cut.
     */
    Q_INVOKABLE void markIn();

    /*!
     * \brief Marks the frame on screen as the end of the cut.
     */
    Q_INVOKABLE void markOut();

    /*!
     * \brief Removes the marked range.
     */
    Q_INVOKABLE void clearMarks();

    /*!
     * \brief Starts the cut.
     */
    Q_INVOKABLE void startCut();

    /*!
     * \brief Sets the player whose frames are marked.
     * \param _player Player, may be nullptr.
     */
    void setPlayer( VideoPlayer *_player );

    /*!
     * \brief Sets the store whose table is cut.
     * \param _metadata Store, may be nullptr.
     */
    void setMetadata( MetadataStore *_metadata );

    bool hasMarkIn() const;
    bool hasMarkOut() const;
    qint64 markInMs() const;
    qint64 markOutMs() const;
    qint64 markInId() const;
    qint64 markOutId() const;
    bool isRunning() const;
    int progress() const;
    QString statusText() const;
    QString resultVideoPath() const;
    QString resultCsvPath() const;
    int keptFrames() const;
    QString errorString() const;

signals:
    void marksChanged();
    void runningChanged();
    void progressChanged();
    void statusChanged();
    void finished();
    void failed();

private:
    /*!
     * \brief Connects the cutter and writes the metadata file of the cut.
     */
    void onCutFinished();

    /*!
     * \brief Sets the last error message and tells the interface.
     */
    void setError( const QString &_text );

    VideoCutter m_cutter;
    VideoPlayer *m_player;
    MetadataStore *m_metadata;

    bool m_hasMarkIn;
    bool m_hasMarkOut;
    qint64 m_markInMs;
    qint64 m_markOutMs;
    qint64 m_markInId;
    qint64 m_markOutId;

    bool m_running;
    int m_progress;
    QString m_statusText;
    QString m_errorString;
    QString m_resultVideoPath;
    QString m_resultCsvPath;
    int m_keptFrames;
};

#endif // CUTCONTROLLER_HPP
