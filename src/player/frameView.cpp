#include "frameView.hpp"

#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>

FrameView::FrameView( QQuickItem *_parent )
    : QQuickItem( _parent )
{
    // The item draws the picture itself, so it has no children and does
    // not need the extra work of a painted item.
    setFlag( QQuickItem::HasContents, true );
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

QRectF FrameView::pictureRect( const QSizeF &_pictureSize ) const
{
    if ( _pictureSize.isEmpty() )
    {
        return QRectF();
    }
    QSizeF target = _pictureSize;
    target.scale( width(), height(), Qt::KeepAspectRatio );
    return QRectF( ( width() - target.width() ) / 2.0,
                   ( height() - target.height() ) / 2.0,
                   target.width(), target.height() );
}

QSGNode *FrameView::updatePaintNode( QSGNode *_node,
                                      UpdatePaintNodeData *_data )
{
    Q_UNUSED( _data );

    // The render thread is only here while the GUI thread is blocked in
    // the sync phase, so reading the current frame is safe here.
    QImage image = ( m_player != nullptr ) ? m_player->currentFrame()
                                           : QImage();

    QSGSimpleTextureNode *node =
        static_cast<QSGSimpleTextureNode *>( _node );
    if ( node == nullptr )
    {
        node = new QSGSimpleTextureNode();
        // The node owns the texture, so the old one is freed with it.
        node->setOwnsTexture( true );
    }

    if ( image.isNull() )
    {
        node->setTexture( nullptr );
        node->setRect( QRectF() );
        return node;
    }

    // A new picture needs a new texture. The old texture is freed with
    // the node, so nothing has to be done for it.
    // The comparison uses the cache key of the image, which changes
    // when the picture changes. Comparing the pictures themselves would
    // read every single pixel, and that is far too expensive.
    if ( m_image.cacheKey() != image.cacheKey() )
    {
        m_image = image;
        QQuickWindow *window = this->window();
        if ( window == nullptr )
        {
            node->setTexture( nullptr );
            node->setRect( QRectF() );
            return node;
        }
        QSGTexture *texture = window->createTextureFromImage( image );
        if ( texture == nullptr )
        {
            node->setTexture( nullptr );
            node->setRect( QRectF() );
            return node;
        }
        // The graphics card scales the picture when the node is drawn,
        // so the scaling costs no processor time.
        texture->setFiltering( QSGTexture::Linear );
        node->setTexture( texture );
    }

    node->setRect( pictureRect( m_image.size() ) );
    return node;
}
