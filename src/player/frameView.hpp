#ifndef FRAMEVIEW_HPP
#define FRAMEVIEW_HPP

#include <QQuickItem>
#include <QPointer>
#include <QImage>

#include "videoPlayer.hpp"

/*!
 * \brief QML item that shows the current frame of a VideoPlayer,
 *        scaled to fit while keeping the aspect ratio.
 *
 * The picture is put on the screen as a texture of the scene graph, not
 * painted with QPainter. Scaling a picture with QPainter costs a lot of
 * processor time: every picture is scaled on the processor and then
 * copied to the graphics card. With a texture the graphics card does
 * the scaling, and the processor only copies the picture once.
 *
 * Threading model:
 * - updatePaintNode() runs on the render thread of the scene graph,
 *   while the GUI thread is blocked in the sync phase. Reading the
 *   current frame there is safe, because the frame is only changed by
 *   the GUI thread, which is busy at that moment.
 * - The picture is taken over as an image, so the render thread works
 *   on its own copy and never touches the buffer of the player.
 */
class FrameView : public QQuickItem
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
     * \brief Builds the node that shows the current picture.
     * \param _node Node of the previous frame, may be nullptr.
     * \return The node of this frame.
     */
    QSGNode *updatePaintNode( QSGNode *_node,
                              UpdatePaintNodeData *_data ) override;

signals:
    void playerChanged();

private:
    /*!
     * \brief Returns the rectangle the picture is shown in.
     *
     * The picture keeps its aspect ratio and is centred in the item.
     * \param _pictureSize Size of the picture.
     * \return The rectangle inside the item.
     */
    QRectF pictureRect( const QSizeF &_pictureSize ) const;

    QPointer<VideoPlayer> m_player;
    // The picture of the last frame, kept so that the node can be built
    // again after a change of the item.
    QImage m_image;
};

#endif // FRAMEVIEW_HPP
