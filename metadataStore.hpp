#ifndef METADATASTORE_HPP
#define METADATASTORE_HPP

#include <QObject>
#include <QAtomicInt>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QSharedPointer>
#include <QUrl>
#include <QVariantMap>
#include <QtConcurrent/QtConcurrentRun>

#include "metadataTable.hpp"

/*!
 * \brief Loads the metadata CSV in the background and looks rows up by
 *        frame id.
 *
 * Threading model:
 * - parseFile() runs on a thread of the global QThreadPool. It only uses
 *   its arguments and builds a new table, so it shares nothing mutable.
 * - The finished table reaches the GUI thread through QFutureWatcher.
 *   From then on it is read-only and can be shared with any thread.
 * - A shared atomic flag lets a newer load cancel an older one, and a
 *   generation number discards a result that arrives too late.
 */
class MetadataStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY( bool loading READ isLoading NOTIFY loadingChanged )
    Q_PROPERTY( bool loaded READ isLoaded NOTIFY tableChanged )
    Q_PROPERTY( QString path READ path NOTIFY tableChanged )
    Q_PROPERTY( QString errorString READ errorString NOTIFY tableChanged )
    Q_PROPERTY( int rowCount READ rowCount NOTIFY tableChanged )
    Q_PROPERTY( qint64 firstId READ firstId NOTIFY tableChanged )
    Q_PROPERTY( qint64 lastId READ lastId NOTIFY tableChanged )
    Q_PROPERTY( int revision READ revision NOTIFY tableChanged )
    Q_PROPERTY( quint64 matchedFrames READ matchedFrames
                NOTIFY statsChanged )
    Q_PROPERTY( quint64 missingRowFrames READ missingRowFrames
                NOTIFY statsChanged )

public:
    /*!
     * \brief Creates an empty store.
     * \param _parent QObject parent.
     */
    explicit MetadataStore( QObject *_parent = nullptr );

    /*!
     * \brief Cancels a load that is still running.
     */
    ~MetadataStore() override;

    /*!
     * \brief Starts loading a CSV file in the background.
     * \param _url Local file URL of the CSV.
     * \return False if the file does not exist.
     */
    Q_INVOKABLE bool load( const QUrl &_url );

    /*!
     * \brief Loads the CSV with the same base name as a video, if any.
     * \param _videoUrl Local file URL of the video.
     * \return True if a matching CSV was found and its load started.
     */
    Q_INVOKABLE bool loadForVideo( const QUrl &_videoUrl );

    /*!
     * \brief Looks up the row of a frame id.
     * \param _frameId Id read from the frame.
     * \return Map with "found", "row", "pan", "tilt", "mode" and "values".
     */
    Q_INVOKABLE QVariantMap rowInfo( qint64 _frameId ) const;

    /*!
     * \brief Returns the trimmed column names of the loaded table.
     */
    Q_INVOKABLE QStringList columnNames() const;

    /*!
     * \brief Counts one displayed frame as matched or missing a row.
     * \param _frameId Id of the frame, -1 if it had no readable bar.
     */
    void recordFrame( qint64 _frameId );

    /*!
     * \brief Clears the matched / missing counters.
     */
    void resetStats();

    /*!
     * \brief Returns the current table, shareable with any thread.
     */
    QSharedPointer<const MetadataTable> table() const;

    /*!
     * \brief Writes a copy of the table with only the given rows.
     *
     * The rows are written in the order of the given ids, so the new
     * file holds exactly the frames of the cut. A row that is not in
     * the table is skipped.
     * \param _targetPath Path of the new CSV file.
     * \param _ids Ids of the frames that belong to the cut.
     * \return True when the file was written.
     */
    bool writeRows( const QString &_targetPath, const QStringList &_ids ) const;

    bool isLoading() const;
    bool isLoaded() const;
    QString path() const;
    QString errorString() const;
    int rowCount() const;
    qint64 firstId() const;
    qint64 lastId() const;
    int revision() const;
    quint64 matchedFrames() const;
    quint64 missingRowFrames() const;

signals:
    void loadingChanged();
    void tableChanged();
    void statsChanged();

private:
    typedef QSharedPointer<MetadataTable> TablePointer;

    /*!
     * \brief Reads and indexes a CSV file. Runs on a pool thread.
     * \param _path Local path of the file.
     * \param _cancel Set to 1 by the GUI thread to stop early.
     * \return A new table; errorString is set on failure.
     */
    static TablePointer parseFile( const QString &_path,
                                   QSharedPointer<QAtomicInt> _cancel );

    /*!
     * \brief Splits one CSV line into trimmed fields.
     * \param _line Raw line without the line ending.
     */
    static QList<QByteArray> splitLine( const QByteArray &_line );

    void onLoadFinished();
    void cancelLoad();

    static const char *const ID_COLUMN_NAME;
    static const int CANCEL_CHECK_INTERVAL = 1000;

    QFutureWatcher<TablePointer> m_watcher;
    QSharedPointer<QAtomicInt> m_cancelFlag;
    quint64 m_loadGeneration;
    quint64 m_watchedGeneration;
    bool m_loading;
    QString m_loadingPath;

    QSharedPointer<const MetadataTable> m_table;
    QString m_errorString;
    int m_revision;
    quint64 m_matchedFrames;
    quint64 m_missingRowFrames;
};

#endif // METADATASTORE_HPP
