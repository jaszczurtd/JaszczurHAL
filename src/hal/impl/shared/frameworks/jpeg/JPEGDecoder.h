/*
JPEGDecoder.h

JPEG Decoder for Arduino
Public domain, Makoto Kurauchi <http://yushakobo.jp>

Adapted by Bodmer for use with a TFT screen

Latest version here:
https://github.com/Bodmer/JPEGDecoder

JaszczurHAL note: this decoder is integrated as a target-neutral, memory-only
utility. Decode JPEG bytes from flash or RAM with decodeArray();
file-based decoding, if ever needed, must go through the JaszczurHAL
filesystem API.
*/

#ifndef JPEGDECODER_H
#define JPEGDECODER_H

#include "hal/hal_config.h"

#ifdef HAL_ENABLE_JPEG

#include <stdint.h>

/* picojpeg reads the compressed stream straight from the caller-provided array,
 * so pgm_read_byte is a plain dereference on every JaszczurHAL target. */
#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#endif

#include "picojpeg.h"

enum { JPEG_ARRAY = 0 };

// #define DEBUG

//------------------------------------------------------------------------------
#ifndef jpg_min
#define jpg_min(a, b) (((a) < (b)) ? (a) : (b))
#endif

//------------------------------------------------------------------------------
typedef unsigned char uint8;
typedef unsigned int uint;
//------------------------------------------------------------------------------

class JPEGDecoder {

private:
  pjpeg_scan_type_t scan_type;
  pjpeg_image_info_t image_info;

  int is_available;
  int mcu_x;
  int mcu_y;
  uint g_nInFileSize;
  uint g_nInFileOfs;
  uint row_pitch;
  uint decoded_width, decoded_height;
  uint row_blocks_per_mcu, col_blocks_per_mcu;
  uint8 status;
  uint8 jpg_source = 0;
  uint8_t *jpg_data;

  static uint8 pjpeg_callback(unsigned char *pBuf, unsigned char buf_size,
                              unsigned char *pBytes_actually_read,
                              void *pCallback_data);
  uint8 pjpeg_need_bytes_callback(unsigned char *pBuf, unsigned char buf_size,
                                  unsigned char *pBytes_actually_read,
                                  void *pCallback_data);
  int decode_mcu(void);
  int decodeCommon(void);

public:
  uint16_t *pImage;
  JPEGDecoder *thisPtr;

  int width;
  int height;
  int comps;
  int MCUSPerRow;
  int MCUSPerCol;
  pjpeg_scan_type_t scanType;
  int MCUWidth;
  int MCUHeight;
  int MCUx;
  int MCUy;

  JPEGDecoder();
  ~JPEGDecoder();

  int available(void);
  int read(void);
  int readSwappedBytes(void);

  int decodeArray(const uint8_t array[], uint32_t array_size);
  void abort(void);
};

extern JPEGDecoder JpegDec;

#endif /* HAL_ENABLE_JPEG */
#endif // JPEGDECODER_H
