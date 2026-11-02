#ifndef BARCODEREADER_HPP
#define BARCODEREADER_HPP

#include <QImage>
#include <QString>
#include <QVector>

#include <cmath>
#include <limits>

/*!
 * \brief Reads the Code 128 frame-id bar drawn at the top-left corner of
 *        each video frame.
 *
 * The bar is a single row of vertical bars, about 7 % of the frame width,
 * starting at x = 0. The reader averages the top rows into one brightness
 * profile, measures bar and space widths with sub-pixel edges, and matches
 * every group of 6 elements against the Code 128 table.
 *
 * Every Code 128 symbol is exactly 11 modules wide, so each group is
 * normalised by its own total width. No module width is assumed, which
 * makes the reader independent of the video resolution as long as one
 * module is at least about 1 pixel wide.
 *
 * Thread safety: read() only uses its arguments, local variables and a
 * constant table, so it can be called from any thread at the same time.
 */
class BarcodeReader
{
public:
    /*!
     * \brief Reads the frame id from an RGBA8888 frame.
     * \param _frame Decoded frame in QImage::Format_RGBA8888.
     * \param frameId_ Receives the decoded id when the function succeeds.
     * \return True if a bar with a valid checksum was found.
     */
    static bool read( const QImage &_frame, qint64 &frameId_ );

private:
    /*!
     * \brief Decodes one brightness profile.
     * \param _profile Mean brightness of each column (0..255).
     * \param text_ Receives the decoded text.
     * \return True on success.
     */
    static bool decodeProfile( const QVector<double> &_profile,
                               QString &text_ );

    /*!
     * \brief Decodes a sequence of element widths starting with a bar.
     * \param _widths Element widths in pixels.
     * \param text_ Receives the decoded text.
     * \return True if start, data, checksum and stop are all valid.
     */
    static bool decodeWidths( const QVector<double> &_widths,
                              QString &text_ );

    /*!
     * \brief Finds the table entry closest to 6 measured widths.
     * \param _widths Widths of one symbol, in pixels.
     * \param _first Index of the first of the 6 widths.
     * \return Symbol value (0..105), STOP_VALUE, or -1 if nothing is close.
     */
    static int matchSymbol( const QVector<double> &_widths, int _first );

    /*!
     * \brief Turns symbol values into text, following code set switches.
     * \param _values Start symbol, data symbols (checksum excluded).
     * \param text_ Receives the decoded text.
     * \return False on an unexpected symbol.
     */
    static bool valuesToText( const QVector<int> &_values, QString &text_ );

    static const double SCAN_WIDTH_RATIO;
    static const double SCAN_HEIGHT_RATIO;
    static const int STOP_VALUE = 106;
};

#endif // BARCODEREADER_HPP
