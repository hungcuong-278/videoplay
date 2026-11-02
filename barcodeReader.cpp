#include "barcodeReader.hpp"

// The bar covers about 120 x 7 pixels on a 1280 x 720 frame.
// Scan a slightly wider area and the rows fully covered by the bar.
const double BarcodeReader::SCAN_WIDTH_RATIO = 140.0 / 1280.0;
const double BarcodeReader::SCAN_HEIGHT_RATIO = 6.0 / 720.0;

namespace
{
// Code 128 patterns, index = symbol value. Each digit is the width of one
// element in modules (bar, space, bar, ...); every pattern sums to 11.
// Index 106 is the stop pattern without its final 2-module bar, which is
// also 11 modules wide and may be cut off at the right edge.
const char *const PATTERNS[] =
{
    "212222", "222122", "222221", "121223", "121322", "131222", "122213",
    "122312", "132212", "221213", "221312", "231212", "112232", "122132",
    "122231", "113222", "123122", "123221", "223211", "221132", "221231",
    "213212", "223112", "312131", "311222", "321122", "321221", "312212",
    "322112", "322211", "212123", "212321", "232121", "111323", "131123",
    "131321", "112313", "132113", "132311", "211313", "231113", "231311",
    "112133", "112331", "132131", "113123", "113321", "133121", "313121",
    "211331", "231131", "213113", "213311", "213131", "311123", "311321",
    "331121", "312113", "312311", "332111", "314111", "221411", "431111",
    "111224", "111422", "121124", "121421", "141122", "141221", "112214",
    "112412", "122114", "122411", "142112", "142211", "241211", "221114",
    "413111", "241112", "134111", "111242", "121142", "121241", "114212",
    "124112", "124211", "411212", "421112", "421211", "212141", "214121",
    "412121", "111143", "111341", "131141", "114113", "114311", "411113",
    "411311", "113141", "114131", "311141", "411131", "211412", "211214",
    "211232", "233111"
};
const int PATTERN_COUNT = 107;
const int MODULES_PER_SYMBOL = 11;
const int ELEMENTS_PER_SYMBOL = 6;
const int START_A = 103;
const int START_B = 104;
const int START_C = 105;
const int CODE_A = 101;
const int CODE_B = 100;
const int CODE_C = 99;
// Sum of absolute differences, in modules, above which a group is
// considered not to be a valid symbol at all.
const double MAX_SYMBOL_ERROR = 2.5;
const double EXTRA_THRESHOLDS[] = { 128.0, 110.0, 145.0 };
const int MIN_EDGES = 20;
}

bool BarcodeReader::read( const QImage &_frame, qint64 &frameId_ )
{
    if ( _frame.isNull() || _frame.format() != QImage::Format_RGBA8888 )
    {
        return false;
    }

    int scanWidth = qMin( _frame.width(),
                          qMax( 32, qRound( _frame.width()
                                            * SCAN_WIDTH_RATIO ) ) );
    int scanRows = qMin( _frame.height(),
                         qMax( 2, qRound( _frame.height()
                                          * SCAN_HEIGHT_RATIO ) ) );

    // Average the top rows column by column: compression noise is reduced
    // while the vertical bars keep their full contrast.
    QVector<double> profile( scanWidth, 0.0 );
    for ( int y = 0; y < scanRows; ++y )
    {
        const uchar *line = _frame.constScanLine( y );
        for ( int x = 0; x < scanWidth; ++x )
        {
            const uchar *pixel = line + x * 4;
            // BT.601 luma, the same weighting the encoder used for Y
            profile[x] += 0.299 * pixel[0] + 0.587 * pixel[1]
                          + 0.114 * pixel[2];
        }
    }
    for ( int x = 0; x < scanWidth; ++x )
    {
        profile[x] /= scanRows;
    }

    QString text;
    if ( !decodeProfile( profile, text ) )
    {
        return false;
    }
    bool isNumber = false;
    qint64 value = text.toLongLong( &isNumber );
    if ( !isNumber )
    {
        return false;
    }
    frameId_ = value;
    return true;
}

bool BarcodeReader::decodeProfile( const QVector<double> &_profile,
                                   QString &text_ )
{
    double minValue = std::numeric_limits<double>::max();
    double maxValue = 0.0;
    for ( double value : _profile )
    {
        minValue = qMin( minValue, value );
        maxValue = qMax( maxValue, value );
    }
    if ( maxValue - minValue < 40.0 )
    {
        return false;   // no contrast: there is no bar in this frame
    }

    // Midpoint between the darkest and brightest column first, then a few
    // fixed levels for frames where one very bright pixel skews the range.
    QVector<double> thresholds;
    thresholds.append( ( minValue + maxValue ) / 2.0 );
    for ( double threshold : EXTRA_THRESHOLDS )
    {
        thresholds.append( threshold );
    }

    for ( double threshold : thresholds )
    {
        // Sub-pixel positions where the profile crosses the threshold
        QVector<double> edges;
        if ( _profile[0] > threshold )
        {
            edges.append( 0.0 );    // a bar touching the left border
        }
        for ( int x = 0; x + 1 < _profile.size(); ++x )
        {
            double a = _profile[x] - threshold;
            double b = _profile[x + 1] - threshold;
            if ( a * b < 0.0 )
            {
                edges.append( x + a / ( a - b ) );
            }
        }
        if ( edges.size() < MIN_EDGES )
        {
            continue;
        }

        QVector<double> widths;
        for ( int i = 1; i < edges.size(); ++i )
        {
            widths.append( edges[i] - edges[i - 1] );
        }
        if ( decodeWidths( widths, text_ ) )
        {
            return true;
        }

        // The first start bar touches x = 0 and is usually lost when the
        // frame is scaled. The next 5 elements of the start symbol are
        // 9 modules wide, which gives the width to restore.
        double moduleWidth = 0.0;
        for ( int i = 0; i < 5 && i < widths.size(); ++i )
        {
            moduleWidth += widths[i];
        }
        moduleWidth /= 9.0;
        QVector<double> restored = widths;
        restored.prepend( 2.0 * moduleWidth );
        if ( decodeWidths( restored, text_ ) )
        {
            return true;
        }
    }
    return false;
}

bool BarcodeReader::decodeWidths( const QVector<double> &_widths,
                                  QString &text_ )
{
    QVector<int> values;
    bool stopFound = false;
    for ( int first = 0; first + ELEMENTS_PER_SYMBOL <= _widths.size();
          first += ELEMENTS_PER_SYMBOL )
    {
        int value = matchSymbol( _widths, first );
        if ( value < 0 )
        {
            return false;
        }
        if ( value == STOP_VALUE )
        {
            stopFound = true;
            break;
        }
        values.append( value );
    }

    // Need start + at least one data symbol + checksum
    if ( !stopFound || values.size() < 3 )
    {
        return false;
    }
    int start = values.first();
    if ( start != START_A && start != START_B && start != START_C )
    {
        return false;
    }

    int checksum = values.last();
    int sum = start;
    for ( int i = 1; i + 1 < values.size(); ++i )
    {
        sum += i * values[i];
    }
    if ( sum % 103 != checksum )
    {
        return false;
    }

    values.removeLast();
    return valuesToText( values, text_ );
}

int BarcodeReader::matchSymbol( const QVector<double> &_widths, int _first )
{
    double total = 0.0;
    for ( int i = 0; i < ELEMENTS_PER_SYMBOL; ++i )
    {
        total += _widths[_first + i];
    }
    if ( total <= 0.0 )
    {
        return -1;
    }
    // Express the 6 widths in modules, using this symbol's own width
    double scale = MODULES_PER_SYMBOL / total;

    int bestValue = -1;
    double bestError = MAX_SYMBOL_ERROR;
    for ( int value = 0; value < PATTERN_COUNT; ++value )
    {
        const char *pattern = PATTERNS[value];
        double error = 0.0;
        for ( int i = 0; i < ELEMENTS_PER_SYMBOL; ++i )
        {
            error += std::fabs( _widths[_first + i] * scale
                                - ( pattern[i] - '0' ) );
        }
        if ( error < bestError )
        {
            bestError = error;
            bestValue = value;
        }
    }
    return bestValue;
}

bool BarcodeReader::valuesToText( const QVector<int> &_values,
                                  QString &text_ )
{
    QString text;
    int codeSet = _values.first();
    for ( int i = 1; i < _values.size(); ++i )
    {
        int value = _values[i];
        if ( codeSet == START_C )
        {
            if ( value < 100 )
            {
                text += QString( "%1" ).arg( value, 2, 10, QChar( '0' ) );
            }
            else if ( value == CODE_B )
            {
                codeSet = START_B;
            }
            else if ( value == CODE_A )
            {
                codeSet = START_A;
            }
            else
            {
                return false;
            }
        }
        else if ( value == CODE_C )
        {
            codeSet = START_C;
        }
        else if ( value < 64 || ( codeSet == START_B && value < 95 ) )
        {
            text += QChar( 32 + value );
        }
        else
        {
            return false;   // control characters are not expected
        }
    }
    text_ = text;
    return !text.isEmpty();
}
