#include "metadataStore.hpp"

const char *const MetadataStore::ID_COLUMN_NAME = "imageId";

MetadataStore::MetadataStore( QObject *_parent )
    : QObject( _parent ),
      m_loadGeneration( 0 ),
      m_watchedGeneration( 0 ),
      m_loading( false ),
      m_revision( 0 ),
      m_matchedFrames( 0 ),
      m_missingRowFrames( 0 )
{
    // QFutureWatcher lives in the GUI thread, so finished() is delivered
    // there even though the work itself runs on a pool thread.
    connect( &m_watcher, &QFutureWatcher<TablePointer>::finished,
             this, &MetadataStore::onLoadFinished );
}

MetadataStore::~MetadataStore()
{
    // The pool thread only holds its own copies (path, cancel flag), so it
    // may safely finish after this object is gone; it just stops early.
    cancelLoad();
}

bool MetadataStore::load( const QUrl &_url )
{
    QString filePath = _url.isLocalFile() ? _url.toLocalFile()
                                          : _url.toString();
    if ( !QFileInfo::exists( filePath ) )
    {
        m_errorString = "File not found: " + filePath;
        ++m_revision;
        emit tableChanged();
        return false;
    }

    cancelLoad();
    m_cancelFlag = QSharedPointer<QAtomicInt>( new QAtomicInt( 0 ) );
    m_watchedGeneration = ++m_loadGeneration;
    m_loadingPath = filePath;
    m_watcher.setFuture( QtConcurrent::run( &MetadataStore::parseFile,
                                            filePath, m_cancelFlag ) );
    if ( !m_loading )
    {
        m_loading = true;
        emit loadingChanged();
    }
    return true;
}

bool MetadataStore::loadForVideo( const QUrl &_videoUrl )
{
    if ( !_videoUrl.isLocalFile() )
    {
        return false;
    }
    QFileInfo video( _videoUrl.toLocalFile() );
    QString candidate = video.absolutePath() + "/"
                        + video.completeBaseName() + ".csv";
    if ( !QFileInfo::exists( candidate ) )
    {
        return false;
    }
    return load( QUrl::fromLocalFile( candidate ) );
}

QVariantMap MetadataStore::rowInfo( qint64 _frameId ) const
{
    QVariantMap info;
    info.insert( "found", false );
    if ( m_table.isNull() || _frameId < 0 )
    {
        return info;
    }
    QHash<qint64, int>::const_iterator it = m_table->rowById.constFind(
        _frameId );
    if ( it == m_table->rowById.constEnd() )
    {
        return info;
    }

    int row = it.value();
    QList<QByteArray> fields = splitLine( m_table->lines.at( row ) );
    QStringList values;
    for ( const QByteArray &field : fields )
    {
        values.append( QString::fromUtf8( field ) );
    }

    info.insert( "found", true );
    info.insert( "row", row );
    info.insert( "values", values );
    const int columns[] = { m_table->panColumn, m_table->tiltColumn,
                            m_table->modeColumn };
    const char *const keys[] = { "pan", "tilt", "mode" };
    for ( int i = 0; i < 3; ++i )
    {
        if ( columns[i] >= 0 && columns[i] < values.size() )
        {
            info.insert( keys[i], values.at( columns[i] ) );
        }
    }
    return info;
}

QStringList MetadataStore::columnNames() const
{
    return m_table.isNull() ? QStringList() : m_table->columns;
}

void MetadataStore::recordFrame( qint64 _frameId )
{
    if ( m_table.isNull() || _frameId < 0 )
    {
        return;
    }
    if ( m_table->rowById.contains( _frameId ) )
    {
        ++m_matchedFrames;
    }
    else
    {
        ++m_missingRowFrames;
    }
    emit statsChanged();
}

void MetadataStore::resetStats()
{
    m_matchedFrames = 0;
    m_missingRowFrames = 0;
    emit statsChanged();
}

QSharedPointer<const MetadataTable> MetadataStore::table() const
{
    return m_table;
}

bool MetadataStore::isLoading() const
{
    return m_loading;
}

bool MetadataStore::isLoaded() const
{
    return !m_table.isNull();
}

QString MetadataStore::path() const
{
    return m_table.isNull() ? QString() : m_table->path;
}

QString MetadataStore::errorString() const
{
    return m_errorString;
}

int MetadataStore::rowCount() const
{
    return m_table.isNull() ? 0 : m_table->lines.size();
}

qint64 MetadataStore::firstId() const
{
    return m_table.isNull() ? -1 : m_table->firstId;
}

qint64 MetadataStore::lastId() const
{
    return m_table.isNull() ? -1 : m_table->lastId;
}

int MetadataStore::revision() const
{
    return m_revision;
}

quint64 MetadataStore::matchedFrames() const
{
    return m_matchedFrames;
}

quint64 MetadataStore::missingRowFrames() const
{
    return m_missingRowFrames;
}

void MetadataStore::onLoadFinished()
{
    // setFuture() already stops watching an older future, the generation
    // check is a second guard against a stale result.
    if ( m_watchedGeneration != m_loadGeneration )
    {
        return;
    }
    TablePointer result = m_watcher.result();
    if ( result.isNull() || !result->errorString.isEmpty() )
    {
        m_errorString = result.isNull() ? QString( "Load cancelled" )
                                        : result->errorString;
    }
    else
    {
        m_errorString.clear();
        m_table = result;   // from now on the table is read-only
        qInfo().noquote()
            << "CSV loaded:" << result->path
            << "rows" << result->lines.size()
            << "ids" << result->firstId << ".." << result->lastId
            << "gaps" << result->missingIds
            << "duplicates" << result->duplicateIds
            << "skipped" << result->skippedLines;
    }
    resetStats();
    ++m_revision;
    // Publish the new table before announcing that loading has ended, so
    // a listener of loadingChanged() already sees the final state.
    m_loading = false;
    emit tableChanged();
    emit loadingChanged();
}

void MetadataStore::cancelLoad()
{
    if ( !m_cancelFlag.isNull() )
    {
        m_cancelFlag->storeRelease( 1 );
    }
}

QList<QByteArray> MetadataStore::splitLine( const QByteArray &_line )
{
    // The metadata files contain plain numbers only: no quoted fields
    QList<QByteArray> fields = _line.split( ',' );
    for ( QByteArray &field : fields )
    {
        field = field.trimmed();
    }
    return fields;
}

MetadataStore::TablePointer MetadataStore::parseFile(
    const QString &_path, QSharedPointer<QAtomicInt> _cancel )
{
    TablePointer table( new MetadataTable );
    table->path = _path;

    QFile file( _path );
    if ( !file.open( QIODevice::ReadOnly ) )
    {
        table->errorString = "Cannot open " + _path;
        return table;
    }

    // Header: locate the id column by name, plus a few columns to display
    table->headerLine = file.readLine();
    while ( table->headerLine.endsWith( '\n' )
            || table->headerLine.endsWith( '\r' ) )
    {
        table->headerLine.chop( 1 );
    }
    QList<QByteArray> header = splitLine( table->headerLine );
    for ( int i = 0; i < header.size(); ++i )
    {
        QString name = QString::fromUtf8( header.at( i ) );
        table->columns.append( name );
        if ( name.compare( ID_COLUMN_NAME, Qt::CaseInsensitive ) == 0 )
        {
            table->idColumn = i;
        }
        else if ( name.compare( "pan", Qt::CaseInsensitive ) == 0 )
        {
            table->panColumn = i;
        }
        else if ( name.compare( "tilt", Qt::CaseInsensitive ) == 0 )
        {
            table->tiltColumn = i;
        }
        else if ( name.compare( "observationMode",
                                Qt::CaseInsensitive ) == 0 )
        {
            table->modeColumn = i;
        }
    }
    if ( table->idColumn < 0 )
    {
        table->errorString = QString( "No '%1' column in %2" )
                                 .arg( ID_COLUMN_NAME, _path );
        return table;
    }

    int lineNumber = 0;
    while ( !file.atEnd() )
    {
        if ( ( ++lineNumber % CANCEL_CHECK_INTERVAL ) == 0
             && _cancel->loadAcquire() != 0 )
        {
            return TablePointer();
        }

        QByteArray line = file.readLine();
        while ( line.endsWith( '\n' ) || line.endsWith( '\r' ) )
        {
            line.chop( 1 );
        }
        if ( line.trimmed().isEmpty() )
        {
            continue;
        }

        QList<QByteArray> fields = splitLine( line );
        bool isNumber = false;
        qint64 id = fields.size() > table->idColumn
                    ? fields.at( table->idColumn ).toLongLong( &isNumber )
                    : 0;
        if ( !isNumber || fields.size() != header.size() )
        {
            ++table->skippedLines;
            continue;
        }
        if ( table->rowById.contains( id ) )
        {
            ++table->duplicateIds;    // keep the first row of an id
            continue;
        }

        table->rowById.insert( id, table->lines.size() );
        table->ids.append( id );
        table->lines.append( line );
        if ( table->firstId < 0 || id < table->firstId )
        {
            table->firstId = id;
        }
        if ( id > table->lastId )
        {
            table->lastId = id;
        }
    }

    if ( table->lines.isEmpty() )
    {
        table->errorString = "No data rows in " + _path;
        return table;
    }
    table->missingIds = static_cast<int>( table->lastId - table->firstId
                                          + 1 - table->lines.size() );
    return table;
}
