#include "cutController.hpp"

#include <QFileInfo>
#include <QMetaObject>

CutController::CutController( QObject *_parent )
    : QObject( _parent ),
      m_player( nullptr ),
      m_metadata( nullptr ),
      m_hasMarkIn( false ),
      m_hasMarkOut( false ),
      m_markInMs( -1 ),
      m_markOutMs( -1 ),
      m_markInId( -1 ),
      m_markOutId( -1 ),
      m_running( false ),
      m_progress( 0 ),
      m_keptFrames( 0 )
{
    connect( &m_cutter, &VideoCutter::runningChanged,
             this, &CutController::runningChanged );
    connect( &m_cutter, &VideoCutter::progressChanged,
             this, &CutController::progressChanged );
    connect( &m_cutter, &VideoCutter::statusChanged, this, [this]()
    {
        m_statusText = m_cutter.statusText();
        emit statusChanged();
    } );
    connect( &m_cutter, &VideoCutter::finished,
             this, &CutController::onCutFinished );
    connect( &m_cutter, &VideoCutter::failed, this, [this]()
    {
        setError( m_cutter.errorString() );
    } );
}

void CutController::markIn()
{
    if ( m_player == nullptr )
    {
        return;
    }
    qint64 frameId = m_player->frameId();
    qint64 frameTime = m_player->frameTime();
    if ( frameId < 0 || frameTime < 0 )
    {
        setError( "The frame on screen has no readable bar" );
        return;
    }
    m_markInMs = frameTime;
    m_markInId = frameId;
    m_hasMarkIn = true;
    // A new mark in makes an old mark out useless, because the range
    // would run backwards.
    if ( m_hasMarkOut && m_markOutMs <= m_markInMs )
    {
        m_hasMarkOut = false;
        m_markOutMs = -1;
        m_markOutId = -1;
    }
    emit marksChanged();
}

void CutController::markOut()
{
    if ( m_player == nullptr )
    {
        return;
    }
    qint64 frameId = m_player->frameId();
    qint64 frameTime = m_player->frameTime();
    if ( frameId < 0 || frameTime < 0 )
    {
        setError( "The frame on screen has no readable bar" );
        return;
    }
    m_markOutMs = frameTime;
    m_markOutId = frameId;
    m_hasMarkOut = true;
    if ( m_hasMarkIn && m_markInMs >= m_markOutMs )
    {
        m_hasMarkIn = false;
        m_markInMs = -1;
        m_markInId = -1;
    }
    emit marksChanged();
}

void CutController::clearMarks()
{
    m_hasMarkIn = false;
    m_hasMarkOut = false;
    m_markInMs = -1;
    m_markOutMs = -1;
    m_markInId = -1;
    m_markOutId = -1;
    emit marksChanged();
}

void CutController::startCut()
{
    if ( m_running )
    {
        setError( "A cut is already running" );
        return;
    }
    if ( !m_hasMarkIn || !m_hasMarkOut )
    {
        setError( "Both a mark in and a mark out are needed" );
        return;
    }
    if ( m_player == nullptr || m_player->source().isEmpty() )
    {
        setError( "No video is open" );
        return;
    }
    if ( m_markOutMs <= m_markInMs )
    {
        setError( "The mark out is before the mark in" );
        return;
    }

    QString videoPath = m_player->source();
    if ( videoPath.startsWith( "file://" ) )
    {
        videoPath = QUrl( videoPath ).toLocalFile();
    }
    else
    {
        setError( "Only a local file can be cut" );
        return;
    }

    // The new file gets its name from the old one, so both belong
    // together.
    QFileInfo videoInfo( videoPath );
    QString base = videoInfo.absolutePath() + QLatin1Char( '/' )
                   + videoInfo.completeBaseName() + "_cut";

    m_running = true;
    m_progress = 0;
    m_keptFrames = 0;
    m_resultVideoPath.clear();
    m_resultCsvPath.clear();
    m_errorString.clear();
    emit runningChanged();
    emit progressChanged();

    m_cutter.start( videoPath, base, m_markInMs, m_markOutMs,
                    m_markInId, m_markOutId );
}

void CutController::onCutFinished()
{
    m_running = false;
    emit runningChanged();

    m_resultVideoPath = m_cutter.resultPath();
    m_keptFrames = m_cutter.keptIds().size();

    // The metadata file of the cut holds exactly the rows of the frames
    // that are really in the new video file. The rows come from the
    // barcode of the pictures, so they are exact and not guessed.
    if ( m_metadata != nullptr && !m_resultVideoPath.isEmpty() )
    {
        QString csvPath = m_resultVideoPath;
        if ( csvPath.endsWith( ".mp4" ) )
        {
            csvPath = csvPath.left( csvPath.size() - 4 );
        }
        csvPath += ".csv";
        if ( m_metadata->writeRows( csvPath, m_cutter.keptIds() ) )
        {
            m_resultCsvPath = csvPath;
        }
        else
        {
            // The table is empty or has none of the ids. The video file
            // is still there, so this is only a note.
            m_statusText = "No metadata file was written";
        }
    }

    m_progress = 100;
    emit progressChanged();
    emit finished();
}

void CutController::setError( const QString &_text )
{
    m_errorString = _text;
    emit failed();
}

void CutController::setPlayer( VideoPlayer *_player )
{
    m_player = _player;
}

void CutController::setMetadata( MetadataStore *_metadata )
{
    m_metadata = _metadata;
}

bool CutController::hasMarkIn() const
{
    return m_hasMarkIn;
}

bool CutController::hasMarkOut() const
{
    return m_hasMarkOut;
}

qint64 CutController::markInMs() const
{
    return m_markInMs;
}

qint64 CutController::markOutMs() const
{
    return m_markOutMs;
}

qint64 CutController::markInId() const
{
    return m_markInId;
}

qint64 CutController::markOutId() const
{
    return m_markOutId;
}

bool CutController::isRunning() const
{
    return m_running;
}

int CutController::progress() const
{
    return m_progress;
}

QString CutController::statusText() const
{
    return m_statusText;
}

QString CutController::resultVideoPath() const
{
    return m_resultVideoPath;
}

QString CutController::resultCsvPath() const
{
    return m_resultCsvPath;
}

int CutController::keptFrames() const
{
    return m_keptFrames;
}

QString CutController::errorString() const
{
    return m_errorString;
}
