#ifndef PANTILTGRAPH_HPP
#define PANTILTGRAPH_HPP

#include <QQuickPaintedItem>
#include <QColor>
#include <QVector>

/*!
 * \brief Shows the pan and tilt of the metadata as a graph.
 *
 * The graph holds the pan and the tilt of every row of the metadata
 * table. The position of the frame on screen is marked, so the change
 * of the camera can be seen while the video is played.
 *
 * The picture is painted with QPainter, which is enough here: the graph
 * only changes when a new table is loaded, not with every picture of the
 * video.
 */
class PanTiltGraph : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY( QColor panColor READ panColor WRITE setPanColor
                NOTIFY colorsChanged )
    Q_PROPERTY( QColor tiltColor READ tiltColor WRITE setTiltColor
                NOTIFY colorsChanged )

public:
    /*!
     * \brief Creates an empty graph.
     * \param _parent Parent item.
     */
    explicit PanTiltGraph( QQuickItem *_parent = nullptr );

    /*!
     * \brief Sets the values that are shown.
     *
     * The values are given in the order of the frames. A value of NaN
     * means that the frame has no value, and the graph leaves a gap
     * there.
     * \param _pan Pan values, may be empty.
     * \param _tilt Tilt values, may be empty.
     */
    Q_INVOKABLE void setValues( const QVector<double> &_pan,
                                const QVector<double> &_tilt );

    /*!
     * \brief Marks a position in the graph.
     * \param _position Position in the list of the values, -1 for none.
     */
    Q_INVOKABLE void setPosition( int _position );

    /*!
     * \brief Returns the color of the pan line.
     */
    QColor panColor() const;

    /*!
     * \brief Sets the color of the pan line.
     * \param _color Color.
     */
    void setPanColor( const QColor &_color );

    /*!
     * \brief Returns the color of the tilt line.
     */
    QColor tiltColor() const;

    /*!
     * \brief Sets the color of the tilt line.
     * \param _color Color.
     */
    void setTiltColor( const QColor &_color );

    /*!
     * \brief Paints the graph.
     * \param _painter Painter provided by the scene graph.
     */
    void paint( QPainter *_painter ) override;

signals:
    void colorsChanged();

private:
    /*!
     * \brief Returns the smallest and the largest value of both lists.
     * \param low_ Receives the smallest value.
     * \param high_ Receives the largest value.
     * \return True when there is a value at all.
     */
    bool valueRange( double &low_, double &high_ ) const;

    /*!
     * \brief Paints one line of the graph.
     * \param _painter Painter provided by the scene graph.
     * \param _values Values of the line.
     * \param _low Smallest value of both lists.
     * \param _high Largest value of both lists.
     * \param _color Color of the line.
     */
    void paintLine( QPainter *_painter, const QVector<double> &_values,
                    double _low, double _high, const QColor &_color ) const;

    QVector<double> m_pan;
    QVector<double> m_tilt;
    int m_position;
    QColor m_panColor;
    QColor m_tiltColor;
};

#endif // PANTILTGRAPH_HPP
