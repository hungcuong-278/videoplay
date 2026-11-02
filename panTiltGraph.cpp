#include "panTiltGraph.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>

PanTiltGraph::PanTiltGraph( QQuickItem *_parent )
    : QQuickPaintedItem( _parent ),
      m_position( -1 ),
      m_panColor( QColor( 80, 160, 255 ) ),
      m_tiltColor( QColor( 255, 170, 80 ) )
{
    setAntialiasing( true );
}

void PanTiltGraph::setValues( const QVector<double> &_pan,
                              const QVector<double> &_tilt )
{
    if ( m_pan == _pan && m_tilt == _tilt )
    {
        return;
    }
    m_pan = _pan;
    m_tilt = _tilt;
    update();
}

void PanTiltGraph::setPosition( int _position )
{
    if ( m_position == _position )
    {
        return;
    }
    m_position = _position;
    update();
}

QColor PanTiltGraph::panColor() const
{
    return m_panColor;
}

void PanTiltGraph::setPanColor( const QColor &_color )
{
    if ( m_panColor == _color )
    {
        return;
    }
    m_panColor = _color;
    emit colorsChanged();
    update();
}

QColor PanTiltGraph::tiltColor() const
{
    return m_tiltColor;
}

void PanTiltGraph::setTiltColor( const QColor &_color )
{
    if ( m_tiltColor == _color )
    {
        return;
    }
    m_tiltColor = _color;
    emit colorsChanged();
    update();
}

bool PanTiltGraph::valueRange( double &low_, double &high_ ) const
{
    bool found = false;
    low_ = 0.0;
    high_ = 0.0;
    for ( const QVector<double> *list : { &m_pan, &m_tilt } )
    {
        for ( double value : *list )
        {
            if ( qIsNaN( value ) )
            {
                continue;
            }
            if ( !found )
            {
                low_ = value;
                high_ = value;
                found = true;
                continue;
            }
            low_ = qMin( low_, value );
            high_ = qMax( high_, value );
        }
    }
    if ( !found )
    {
        return false;
    }
    // A single value would give a range of zero, so a small range is
    // used instead, with the value in the middle.
    if ( high_ - low_ < 1e-9 )
    {
        low_ -= 0.5;
        high_ += 0.5;
    }
    return true;
}

void PanTiltGraph::paintLine( QPainter *_painter,
                              const QVector<double> &_values,
                              double _low, double _high,
                              const QColor &_color ) const
{
    if ( _values.isEmpty() )
    {
        return;
    }
    const int count = _values.size();
    const double width = this->width();
    const double height = this->height();
    const double step = count > 1 ? width / ( count - 1 ) : 0.0;

    QPainterPath path;
    bool havePoint = false;
    for ( int i = 0; i < count; ++i )
    {
        double value = _values.at( i );
        if ( qIsNaN( value ) )
        {
            // The frame has no value, so the line has a gap there.
            havePoint = false;
            continue;
        }
        double x = i * step;
        double y = height - ( value - _low ) / ( _high - _low ) * height;
        if ( !havePoint )
        {
            path.moveTo( x, y );
            havePoint = true;
        }
        else
        {
            path.lineTo( x, y );
        }
    }

    _painter->setPen( QPen( _color, 1.5 ) );
    _painter->drawPath( path );
}

void PanTiltGraph::paint( QPainter *_painter )
{
    _painter->setRenderHint( QPainter::Antialiasing, true );
    _painter->fillRect( QRectF( 0, 0, width(), height() ),
                        QColor( 20, 20, 24 ) );

    double low = 0.0;
    double high = 1.0;
    bool haveRange = valueRange( low, high );

    // The frame that is on screen is marked with a line, so the change
    // of the camera can be followed while the video is played.
    int count = qMax( m_pan.size(), m_tilt.size() );
    if ( m_position >= 0 && m_position < count )
    {
        double step = count > 1 ? width() / ( count - 1 ) : 0.0;
        double x = m_position * step;
        _painter->setPen( QPen( QColor( 255, 255, 255, 140 ), 1 ) );
        _painter->drawLine( QPointF( x, 0 ), QPointF( x, height() ) );
    }

    if ( !haveRange )
    {
        _painter->setPen( QPen( QColor( 120, 120, 120 ) ) );
        _painter->drawText( QRectF( 0, 0, width(), height() ),
                            Qt::AlignCenter, "no pan / tilt data" );
        return;
    }

    paintLine( _painter, m_pan, low, high, m_panColor );
    paintLine( _painter, m_tilt, low, high, m_tiltColor );

    // A small legend, so the two lines can be told apart.
    _painter->setPen( QPen( m_panColor ) );
    _painter->drawText( 6, 14, "pan" );
    _painter->setPen( QPen( m_tiltColor ) );
    _painter->drawText( 6, 28, "tilt" );
}
