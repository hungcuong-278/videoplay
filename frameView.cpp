#include "frameView.hpp"

FrameView::FrameView( QQuickItem *_parent )
    : QQuickPaintedItem( _parent )
{
    setFillColor( Qt::black );
}

VideoPlayer *FrameView::player() const
{
    return m_player;
}

void FrameView::setPlayer( VideoPlayer *_player )
{
    if ( _player == m_player )
    {
        return;
    }
    if ( m_player != nullptr )
    {
        disconnect( m_player, nullptr, this, nullptr );
    }
    m_player = _player;
    if ( m_player != nullptr )
    {
        // Both objects live in the GUI thread: this is a direct call.
        connect( m_player, &VideoPlayer::frameReady,
                 this, [this]() { update(); } );
    }
    emit playerChanged();
    update();
}

void FrameView::paint( QPainter *_painter )
{
    if ( m_player == nullptr )
    {
        return;
    }
    QImage frame = m_player->currentFrame();
    if ( frame.isNull() )
    {
        return;
    }

    QSizeF target = frame.size();
    target.scale( width(), height(), Qt::KeepAspectRatio );
    QRectF rect( ( width() - target.width() ) / 2.0,
                 ( height() - target.height() ) / 2.0,
                 target.width(), target.height() );

    _painter->setRenderHint( QPainter::SmoothPixmapTransform, true );
    _painter->drawImage( rect, frame );
}
