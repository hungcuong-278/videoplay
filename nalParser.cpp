#include "nalParser.hpp"

int NalParser::codecFromCaps( GstCaps *_caps )
{
    if ( _caps == nullptr || gst_caps_is_empty( _caps ) )
    {
        return CODEC_OTHER;
    }
    const GstStructure *structure = gst_caps_get_structure( _caps, 0 );
    const char *name = gst_structure_get_name( structure );
    if ( g_strcmp0( name, "video/x-h264" ) == 0 )
    {
        return CODEC_H264;
    }
    if ( g_strcmp0( name, "video/x-h265" ) == 0 )
    {
        return CODEC_H265;
    }
    return CODEC_OTHER;
}

const char *NalParser::codecName( int _codec )
{
    switch ( _codec )
    {
    case CODEC_H264:
        return "H.264";
    case CODEC_H265:
        return "H.265";
    default:
        return "unknown";
    }
}

int NalParser::lengthSizeFromCaps( GstCaps *_caps )
{    if ( _caps == nullptr || gst_caps_is_empty( _caps ) )
    {
        return 0;
    }
    const GstStructure *structure = gst_caps_get_structure( _caps, 0 );
    const char *name = gst_structure_get_name( structure );
    bool h264 = ( g_strcmp0( name, "video/x-h264" ) == 0 );
    bool h265 = ( g_strcmp0( name, "video/x-h265" ) == 0 );
    if ( !h264 && !h265 )
    {
        return 0;
    }

    // A byte stream separates its NAL units with start codes and has no
    // length prefix. An avc or hvc stream prefixes every NAL unit with
    // its length, whose size is stored in the codec data.
    const char *format = gst_structure_get_string( structure,
                                                   "stream-format" );
    if ( format == nullptr
         || g_strcmp0( format, "byte-stream" ) == 0 )
    {
        return 0;
    }

    int lengthSize = 4;
    const GValue *value = gst_structure_get_value( structure, "codec_data" );
    GstBuffer *config = ( value != nullptr )
                        ? gst_value_get_buffer( value ) : nullptr;
    GstMapInfo map;
    if ( config != nullptr
         && gst_buffer_map( config, &map, GST_MAP_READ ) )
    {
        if ( h264 && map.size > 4 )
        {
            lengthSize = ( map.data[4] & 0x03 ) + 1;       // avcC
        }
        else if ( h265 && map.size > 21 )
        {
            lengthSize = ( map.data[21] & 0x03 ) + 1;      // hvcC
        }
        gst_buffer_unmap( config, &map );
    }
    return lengthSize;
}

int NalParser::pictureNalType( int _codec, int _lengthSize,
                               GstBuffer *_buffer, int _scanLimit )
{
    // Returns the NAL type of the first picture (VCL) NAL unit of an
    // access unit, or -1 if there is none.
    if ( _buffer == nullptr || _codec == CODEC_OTHER )
    {
        return -1;
    }
    GstMapInfo map;
    if ( !gst_buffer_map( _buffer, &map, GST_MAP_READ ) )
    {
        return -1;
    }
    const guint8 *data = map.data;
    gsize size = map.size;
    if ( _scanLimit > 0 && static_cast<gsize>( _scanLimit ) < size )
    {
        size = static_cast<gsize>( _scanLimit );
    }
    bool h264 = ( _codec == CODEC_H264 );
    int result = -1;

    // Visit the first byte of every NAL unit in the access unit
    gsize pos = 0;
    while ( result < 0 && pos < size )
    {
        gsize nal = 0;
        gsize next = size;
        if ( _lengthSize > 0 )
        {
            if ( pos + _lengthSize > size )
            {
                break;
            }
            gsize length = 0;
            for ( int i = 0; i < _lengthSize; ++i )
            {
                length = ( length << 8 ) | data[pos + i];
            }
            nal = pos + _lengthSize;
            next = nal + length;
        }
        else
        {
            // Find the next 00 00 01 start code
            gsize i = pos;
            while ( i + 2 < size
                    && !( data[i] == 0 && data[i + 1] == 0
                          && data[i + 2] == 1 ) )
            {
                ++i;
            }
            if ( i + 3 >= size )
            {
                break;
            }
            nal = i + 3;
            next = nal;
        }
        if ( nal < size )
        {
            int type = h264 ? ( data[nal] & 0x1f )
                            : ( ( data[nal] >> 1 ) & 0x3f );
            // VCL types: H.264 1..5, H.265 0..31
            bool vcl = h264 ? ( type >= 1 && type <= 5 ) : ( type <= 31 );
            if ( vcl )
            {
                result = type;
            }
        }
        pos = next;
    }
    gst_buffer_unmap( _buffer, &map );
    return result;
}

bool NalParser::isEntryPoint( int _codec, int _type )
{
    if ( _type < 0 )
    {
        return false;
    }
    if ( _codec == CODEC_H264 )
    {
        return _type == 5;              // IDR
    }
    if ( _codec == CODEC_H265 )
    {
        return _type >= 16 && _type <= 21;     // IRAP
    }
    return false;
}

bool NalParser::isRasl( int _codec, int _type )
{
    if ( _codec != CODEC_H265 || _type < 0 )
    {
        return false;
    }
    return _type == 8 || _type == 9;   // RASL, RASL_R
}
