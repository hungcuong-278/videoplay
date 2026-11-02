#ifndef FRAMEVIEW_HPP
#define FRAMEVIEW_HPP

#include <QQuickPaintedItem>
#include <QPainter>
#include <QPointer>
#include <QImage>

#include "videoPlayer.hpp"

/*!
 * \brief QML item that paints the current frame of a VideoPlayer,
 *        scaled to fit while keeping the aspect ratio.
 *
 * It lives in the GUI thread and only reads frames that VideoPlayer has
 * already handed over to the GUI thread, so it needs no locking.
 */
class FrameView : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY( VideoPlayer *player READ player WRITE setPlayer
                NOTIFY playerChanged )

public:
    /*!
     * \brief Creates an empty view.
     * \param _parent Parent item.
     */
    explicit FrameView( QQuickItem *_parent = nullptr );

    /*!
     * \brief Returns the player whose frames are shown.
     */
    VideoPlayer *player() const;

    /*!
     * \brief Sets the player whose frames are shown.
     * \param _player Player, may be nullptr.
     */
    void setPlayer( VideoPlayer *_player );

    /*!
     * \brief Paints the current frame letterboxed on a black background.
     * \param _painter Painter provided by the scene graph.
     */
    void paint( QPainter *_painter ) override;

signals:
    void playerChanged();

private:
    QPointer<VideoPlayer> m_player;
};

#endif // FRAMEVIEW_HPP
