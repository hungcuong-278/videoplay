#ifndef NALPARSER_HPP
#define NALPARSER_HPP

#include <gst/gst.h>

/*!
 * \brief Recognises the structure of H.264 and H.265 elementary streams.
 *
 * Two parts of the program need the same answers about a stream:
 * - the decoder gate of the player, which must know whether a buffer
 *   starts with an entry point picture (IDR or IRAP);
 * - the keyframe index, which scans a file for keyframes without decoding
 *   it.
 *
 * Both read the NAL units of a buffer, so the code lives in one place and
 * cannot drift apart.
 *
 * Thread safety: every function uses only its arguments and local
 * variables, so it can be called from any thread at the same time.
 */
class NalParser
{
public:
    /*!
     * \brief Video codecs the parser knows about.
     */
    enum CodecKind
    {
        CODEC_OTHER = 0,
        CODEC_H264 = 1,
        CODEC_H265 = 2
    };

    /*!
     * \brief Returns the codec described by a set of caps.
     * \param _caps Caps of a parsed video stream, may be nullptr.
     * \return CODEC_H264, CODEC_H265 or CODEC_OTHER.
     */
    static int codecFromCaps( GstCaps *_caps );

    /*!
     * \brief Returns the size of the NAL length prefix of a stream.
     *
     * A byte stream (MPEG-TS, elementary streams) separates its NAL units
     * with start codes and has no length prefix. An avc or hvc stream
     * (MP4, Matroska) prefixes every NAL unit with its length, whose size
     * is stored in the codec data.
     * \param _caps Caps of a parsed video stream, may be nullptr.
     * \return Length prefix size in bytes, 0 when start codes are used.
     */
    static int lengthSizeFromCaps( GstCaps *_caps );

    /*!
     * \brief Returns the type of the first picture NAL unit of a buffer.
     * \param _codec CodecKind of the stream.
     * \param _lengthSize Length prefix size in bytes, 0 for start codes.
     * \param _buffer Buffer holding one access unit.
     * \param _scanLimit Number of bytes to look at, 0 for the whole buffer.
     * \return NAL type, or -1 when the buffer has no picture NAL unit.
     */
    static int pictureNalType( int _codec, int _lengthSize,
                               GstBuffer *_buffer, int _scanLimit = 0 );

    /*!
     * \brief Tells whether a NAL type is an entry point picture.
     * \param _codec CodecKind of the stream.
     * \param _type NAL type as returned by pictureNalType().
     * \return True for an H.264 IDR or an H.265 IRAP picture.
     */
    static bool isEntryPoint( int _codec, int _type );

    /*!
     * \brief Tells whether a NAL type is a RASL picture.
     *
     * A RASL picture of H.265 refers to the pictures before the CRA it
     * follows, so it cannot be decoded after a seek.
     * \param _codec CodecKind of the stream.
     * \param _type NAL type as returned by pictureNalType().
     * \return True for a RASL picture.
     */
    static bool isRasl( int _codec, int _type );
};

#endif // NALPARSER_HPP
