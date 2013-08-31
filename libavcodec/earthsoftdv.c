/*
 * Earthsoft PV3/PV4 DV decoder
 * Copyright (C) 2009-2013 smdn (http://smdn.jp/)
 *
 * This file is patch for decoding Earthsoft PV3/PV4 DV with Libav,
 * and is licensed under MIT/X11.
 *
 * This patch is not merged to Libav's repos yet, so don't ask to
 * Libav dev-team any questions about this file.
 *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * Reference documents:
 *   PV3/PV4 online product documentation (Japanese)
 *     http://earthsoft.jp/PV/index.html
 *   PV3/PV4 hardware specification
 *     http://earthsoft.jp/PV/specification.html
 *   PV3/PV4 file format documentation
 *     http://earthsoft.jp/PV/tech-file.html
 *   PV3/PV4 codec documentation
 *     http://earthsoft.jp/PV/tech-codec.html
 *   SMPTE 370M-2006 (IDCT and VLC)
 */

/**
 * @file libavcodec/earthsoftdv.c
 * Earthsoft PV3/PV4 DV decoder.
 */
#include "avcodec.h"
#include "dsputil.h"
#include "internal.h"
#include "get_bits.h"
#include "put_bits.h"
#include "simple_idct.h"
#include "earthsoftdv.h"

#if CONFIG_EARTHSOFTDV_DECODER

#define AV_LOG_MORE_VERBOSE (AV_LOG_DEBUG + 1)
#undef AV_LOG_MORE_VERBOSE

/*
 * codewords and run length of VLC specified in SMPTE 370M-2006
 */
#define VLC_BITS 15
#define NB_VLC 378

static const uint8_t vlc_run[NB_VLC] = {
   0,  0,  0,  1,  0,  0,  2,  1,
   0,  0,  3,  4,  0,  0,  5,  6,
   2,  1,  1,  0,  0,  0,  7,  8,
   9, 10,  3,  4,  2,  1,  1,  1,
   0,  0,  0,  0,  0,  0, 11, 12,
  13, 14,  5,  6,  3,  4,  2,  2,
   1,  0,  0,  0,  0,  0,  5,  3,
   3,  2,  1,  1,  1,  0,  1,  6,
   4,  3,  1,  1,  1,  2,  3,  4,
   5,  7,  8,  9, 10,  7,  8,  4,
   3,  2,  2,  2,  2,  2,  1,  1,
   1,  6,  7,  8,  9, 10, 11, 12,
  13, 14, 15, 16, 17, 18, 19, 20,
  21, 22, 23, 24, 25, 26, 27, 28,
  29, 30, 31, 32, 33, 34, 35, 36,
  37, 38, 39, 40, 41, 42, 43, 44,
  45, 46, 47, 48, 49, 50, 51, 52,
  53, 54, 55, 56, 57, 58, 59, 60,
  61,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,
};

static const uint8_t vlc_level[NB_VLC] = {
    1,   2,   0,   1,   3,   4,   1,   2,
    5,   6,   1,   1,   7,   8,   1,   1,
    2,   3,   4,   9,  10,  11,   1,   1,
    1,   1,   2,   2,   3,   5,   6,   7,
   12,  13,  14,  15,  16,  17,   1,   1,
    1,   1,   2,   2,   3,   3,   4,   5,
    8,  18,  19,  20,  21,  22,   3,   4,
    5,   6,   9,  10,  11,   0,   0,   3,
    4,   6,  12,  13,  14,   0,   0,   0,
    0,   2,   2,   2,   2,   3,   3,   5,
    7,   7,   8,   9,  10,  11,  15,  16,
   17,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
    0,  23,  24,  25,  26,  27,  28,  29,
   30,  31,  32,  33,  34,  35,  36,  37,
   38,  39,  40,  41,  42,  43,  44,  45,
   46,  47,  48,  49,  50,  51,  52,  53,
   54,  55,  56,  57,  58,  59,  60,  61,
   62,  63,  64,  65,  66,  67,  68,  69,
   70,  71,  72,  73,  74,  75,  76,  77,
   78,  79,  80,  81,  82,  83,  84,  85,
   86,  87,  88,  89,  90,  91,  92,  93,
   94,  95,  96,  97,  98,  99, 100, 101,
  102, 103, 104, 105, 106, 107, 108, 109,
  110, 111, 112, 113, 114, 115, 116, 117,
  118, 119, 120, 121, 122, 123, 124, 125,
  126, 127, 128, 129, 130, 131, 132, 133,
  134, 135, 136, 137, 138, 139, 140, 141,
  142, 143, 144, 145, 146, 147, 148, 149,
  150, 151, 152, 153, 154, 155, 156, 157,
  158, 159, 160, 161, 162, 163, 164, 165,
  166, 167, 168, 169, 170, 171, 172, 173,
  174, 175, 176, 177, 178, 179, 180, 181,
  182, 183, 184, 185, 186, 187, 188, 189,
  190, 191, 192, 193, 194, 195, 196, 197,
  198, 199, 200, 201, 202, 203, 204, 205,
  206, 207, 208, 209, 210, 211, 212, 213,
  214, 215, 216, 217, 218, 219, 220, 221,
  222, 223, 224, 225, 226, 227, 228, 229,
  230, 231, 232, 233, 234, 235, 236, 237,
  238, 239, 240, 241, 242, 243, 244, 245,
  246, 247, 248, 249, 250, 251, 252, 253,
  254, 255,
};

static const uint8_t vlc_bits[NB_VLC] = {
   2,  3,  4,  4,  4,  4,  5,  5,
   5,  5,  6,  6,  6,  6,  7,  7,
   7,  7,  7,  7,  7,  7,  8,  8,
   8,  8,  8,  8,  8,  8,  8,  8,
   8,  8,  8,  8,  8,  8,  9,  9,
   9,  9,  9,  9,  9,  9,  9,  9,
   9,  9,  9,  9,  9,  9, 10, 10,
  10, 10, 10, 10, 10, 11, 11, 11,
  11, 11, 11, 11, 11, 12, 12, 12,
  12, 12, 12, 12, 12, 12, 12, 12,
  12, 12, 12, 12, 12, 12, 12, 12,
  12, 13, 13, 13, 13, 13, 13, 13,
  13, 13, 13, 13, 13, 13, 13, 13,
  13, 13, 13, 13, 13, 13, 13, 13,
  13, 13, 13, 13, 13, 13, 13, 13,
  13, 13, 13, 13, 13, 13, 13, 13,
  13, 13, 13, 13, 13, 13, 13, 13,
  13, 13, 13, 13, 13, 13, 13, 13,
  13, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15,
  15, 15,
};

static const uint16_t vlc_code[NB_VLC] = {
  0x0000, 0x0002, 0x0006, 0x0007, 0x0008, 0x0009, 0x0014, 0x0015,
  0x0016, 0x0017, 0x0030, 0x0031, 0x0032, 0x0033, 0x0068, 0x0069,
  0x006a, 0x006b, 0x006c, 0x006d, 0x006e, 0x006f, 0x00e0, 0x00e1,
  0x00e2, 0x00e3, 0x00e4, 0x00e5, 0x00e6, 0x00e7, 0x00e8, 0x00e9,
  0x00ea, 0x00eb, 0x00ec, 0x00ed, 0x00ee, 0x00ef, 0x01e0, 0x01e1,
  0x01e2, 0x01e3, 0x01e4, 0x01e5, 0x01e6, 0x01e7, 0x01e8, 0x01e9,
  0x01ea, 0x01eb, 0x01ec, 0x01ed, 0x01ee, 0x01ef, 0x03e0, 0x03e1,
  0x03e2, 0x03e3, 0x03e4, 0x03e5, 0x03e6, 0x07ce, 0x07cf, 0x07d0,
  0x07d1, 0x07d2, 0x07d3, 0x07d4, 0x07d5, 0x0fac, 0x0fad, 0x0fae,
  0x0faf, 0x0fb0, 0x0fb1, 0x0fb2, 0x0fb3, 0x0fb4, 0x0fb5, 0x0fb6,
  0x0fb7, 0x0fb8, 0x0fb9, 0x0fba, 0x0fbb, 0x0fbc, 0x0fbd, 0x0fbe,
  0x0fbf, 0x1f86, 0x1f87, 0x1f88, 0x1f89, 0x1f8a, 0x1f8b, 0x1f8c,
  0x1f8d, 0x1f8e, 0x1f8f, 0x1f90, 0x1f91, 0x1f92, 0x1f93, 0x1f94,
  0x1f95, 0x1f96, 0x1f97, 0x1f98, 0x1f99, 0x1f9a, 0x1f9b, 0x1f9c,
  0x1f9d, 0x1f9e, 0x1f9f, 0x1fa0, 0x1fa1, 0x1fa2, 0x1fa3, 0x1fa4,
  0x1fa5, 0x1fa6, 0x1fa7, 0x1fa8, 0x1fa9, 0x1faa, 0x1fab, 0x1fac,
  0x1fad, 0x1fae, 0x1faf, 0x1fb0, 0x1fb1, 0x1fb2, 0x1fb3, 0x1fb4,
  0x1fb5, 0x1fb6, 0x1fb7, 0x1fb8, 0x1fb9, 0x1fba, 0x1fbb, 0x1fbc,
  0x1fbd, 0x7f17, 0x7f18, 0x7f19, 0x7f1a, 0x7f1b, 0x7f1c, 0x7f1d,
  0x7f1e, 0x7f1f, 0x7f20, 0x7f21, 0x7f22, 0x7f23, 0x7f24, 0x7f25,
  0x7f26, 0x7f27, 0x7f28, 0x7f29, 0x7f2a, 0x7f2b, 0x7f2c, 0x7f2d,
  0x7f2e, 0x7f2f, 0x7f30, 0x7f31, 0x7f32, 0x7f33, 0x7f34, 0x7f35,
  0x7f36, 0x7f37, 0x7f38, 0x7f39, 0x7f3a, 0x7f3b, 0x7f3c, 0x7f3d,
  0x7f3e, 0x7f3f, 0x7f40, 0x7f41, 0x7f42, 0x7f43, 0x7f44, 0x7f45,
  0x7f46, 0x7f47, 0x7f48, 0x7f49, 0x7f4a, 0x7f4b, 0x7f4c, 0x7f4d,
  0x7f4e, 0x7f4f, 0x7f50, 0x7f51, 0x7f52, 0x7f53, 0x7f54, 0x7f55,
  0x7f56, 0x7f57, 0x7f58, 0x7f59, 0x7f5a, 0x7f5b, 0x7f5c, 0x7f5d,
  0x7f5e, 0x7f5f, 0x7f60, 0x7f61, 0x7f62, 0x7f63, 0x7f64, 0x7f65,
  0x7f66, 0x7f67, 0x7f68, 0x7f69, 0x7f6a, 0x7f6b, 0x7f6c, 0x7f6d,
  0x7f6e, 0x7f6f, 0x7f70, 0x7f71, 0x7f72, 0x7f73, 0x7f74, 0x7f75,
  0x7f76, 0x7f77, 0x7f78, 0x7f79, 0x7f7a, 0x7f7b, 0x7f7c, 0x7f7d,
  0x7f7e, 0x7f7f, 0x7f80, 0x7f81, 0x7f82, 0x7f83, 0x7f84, 0x7f85,
  0x7f86, 0x7f87, 0x7f88, 0x7f89, 0x7f8a, 0x7f8b, 0x7f8c, 0x7f8d,
  0x7f8e, 0x7f8f, 0x7f90, 0x7f91, 0x7f92, 0x7f93, 0x7f94, 0x7f95,
  0x7f96, 0x7f97, 0x7f98, 0x7f99, 0x7f9a, 0x7f9b, 0x7f9c, 0x7f9d,
  0x7f9e, 0x7f9f, 0x7fa0, 0x7fa1, 0x7fa2, 0x7fa3, 0x7fa4, 0x7fa5,
  0x7fa6, 0x7fa7, 0x7fa8, 0x7fa9, 0x7faa, 0x7fab, 0x7fac, 0x7fad,
  0x7fae, 0x7faf, 0x7fb0, 0x7fb1, 0x7fb2, 0x7fb3, 0x7fb4, 0x7fb5,
  0x7fb6, 0x7fb7, 0x7fb8, 0x7fb9, 0x7fba, 0x7fbb, 0x7fbc, 0x7fbd,
  0x7fbe, 0x7fbf, 0x7fc0, 0x7fc1, 0x7fc2, 0x7fc3, 0x7fc4, 0x7fc5,
  0x7fc6, 0x7fc7, 0x7fc8, 0x7fc9, 0x7fca, 0x7fcb, 0x7fcc, 0x7fcd,
  0x7fce, 0x7fcf, 0x7fd0, 0x7fd1, 0x7fd2, 0x7fd3, 0x7fd4, 0x7fd5,
  0x7fd6, 0x7fd7, 0x7fd8, 0x7fd9, 0x7fda, 0x7fdb, 0x7fdc, 0x7fdd,
  0x7fde, 0x7fdf, 0x7fe0, 0x7fe1, 0x7fe2, 0x7fe3, 0x7fe4, 0x7fe5,
  0x7fe6, 0x7fe7, 0x7fe8, 0x7fe9, 0x7fea, 0x7feb, 0x7fec, 0x7fed,
  0x7fee, 0x7fef, 0x7ff0, 0x7ff1, 0x7ff2, 0x7ff3, 0x7ff4, 0x7ff5,
  0x7ff6, 0x7ff7, 0x7ff8, 0x7ff9, 0x7ffa, 0x7ffb, 0x7ffc, 0x7ffd,
  0x7ffe, 0x7fff,
};

typedef struct EarthsoftDVDecodeBlockContext {
    AVCodecContext *avctx;
    int block_index;
    int interlaced;
    int width, height;
    int16_t *lum_quants;
    int16_t *chrom_quants;
    uint8_t *scantable;
    int nb_mb;
    int nb_mb_per_line;
    int mb_pad_x;
    int mb_pad_y;
    int mb_bottom_y;
    int nb_frames;
    AVFrame *picture;
    DSPContext dsp;
    GetBitContext gb;
} EarthsoftDVDecodeBlockContext;

typedef struct EarthsoftDVDecodeContext {
    AVCodecContext *avctx;
    AVFrame picture;
    DSPContext dsp;
    int initialized;
    int nb_frames;
    int nb_blocks;
    int interlaced;
    int width, height;
    int16_t lum_quants[64];
    int16_t chrom_quants[64];
    uint8_t scantable[64];
    EarthsoftDVDecodeBlockContext *block_context[4];
} EarthsoftDVDecodeContext;

typedef struct VLCNode VLCNode;

struct VLCNode {
    VLCNode *zero;
    VLCNode *one;
    int level;
    int run;
    int bits;
};

//#define NB_VLC_USE 91 // 89 (0 <= bits <= 12) + 1 (bits == 13) + 1 (bits == 15)
#define NB_VLC_USE 190
static VLCNode vlc_tree[NB_VLC_USE];

static int esdv_build_vlc(AVCodecContext *avctx)
{
    static int done = 0;
    int i, l, bits;
    int node;
    int bit;
    uint16_t code;
    VLCNode *current;
    int done_run = 0;
    int done_level = 0;

    if (done)
        return 0;

    memset(vlc_tree, 0, sizeof(vlc_tree));

    node = 1;

    for (i = 0; i < FF_ARRAY_ELEMS(vlc_code); i++) {
        code    = vlc_code[i];
        bits    = vlc_bits[i];
        current = vlc_tree;

        if (bits == 13) {
            /*
             * a pair of (run, 0)
             * leading 7 bits(1111110b) are codeword for (run, 0),
             * and following 6 bits are binary notation of run (6 to 61).
             * insert leading 7 bits into tree for optimization.
             */
            if (done_run)
                continue;

            bits = 7;
            code = 0x7e;
            done_run = 1;
        }
        else if (bits == 15) {
            /*
             * a pair of (0, level)
             * leading 7 bits(1111111b) are codeword for (0, level),
             * and following 8 bits are binary notation of run (23 to 255).
             * insert leading 7 bits into tree for optimization.
             */
            if (done_level)
                continue;

            bits = 7;
            code = 0x7f;
            done_level = 1;
        }

        for (l = bits - 1;; l--) {
            bit = (code >> l) & 0x1;

            if (bit) {
                if (current->one == NULL)
                    current->one = &vlc_tree[node++];
                current = current->one;
            }
            else {
                if (current->zero == NULL)
                    current->zero = &vlc_tree[node++];
                current = current->zero;
            }

            if (NB_VLC_USE <= node) {
                av_log(avctx, AV_LOG_ERROR, "vlc tree overflow\n");
                return -1;
            }

            if (l == 0) {
                current->level = vlc_level[i];
                current->run   = vlc_run[i];
                current->bits  = vlc_bits[i];
                current->zero  = NULL;
                current->one   = NULL;
                break;
            }
        }
    }

    done = 1;

    return 0;
}

static av_cold int esdv_decode_init(AVCodecContext *avctx)
{
    EarthsoftDVDecodeContext *s = avctx->priv_data;
    int ret;

    s->avctx = avctx;
    s->initialized = 0;
    s->nb_frames = 0;

    avcodec_get_frame_defaults(&s->picture);
    avctx->coded_frame = &s->picture;

    ff_dsputil_init(&s->dsp, avctx);

    ret = esdv_build_vlc(avctx);

    return ret;
}

static void esdv_decode_free(EarthsoftDVDecodeContext *s)
{
    int i;

    for (i = 0; i < s->nb_blocks; i++) {
        av_freep(&s->block_context[i]);
    }
}

static int esdv_decode_init_context(EarthsoftDVDecodeContext *s, EarthsoftDVVideoContext *video)
{
    EarthsoftDVDecodeBlockContext *bc;
    int i;
    int nb_mb_total;
    int nb_mb_per_line;
    int nb_mb_pad;
    int mb_pad_start_y;

    s->width      = video->width;
    s->height     = video->height;
    s->interlaced = video->interlaced;

    for (i = 0; i < 64; i++) {
        s->lum_quants  [i] = video->lum_quants      [ff_zigzag_direct[i]];
        s->chrom_quants[i] = video->chrom_quants    [ff_zigzag_direct[i]];
        s->scantable   [i] = s->dsp.idct_permutation[ff_zigzag_direct[i]];
    }

    if (s->lum_quants[0] && s->lum_quants[0] != 32)
        av_log(s->avctx, AV_LOG_WARNING, "luminance DC coef = %d, but it will be ignored (DC coef is fixed to 32 by codec specification)\n", s->lum_quants[0]);

    if (s->chrom_quants[0] && s->chrom_quants[0] != 32)
        av_log(s->avctx, AV_LOG_WARNING, "chrominance DC coef = %d, but it will be ignored (DC coef is fixed to 32 by codec specification)\n", s->chrom_quants[0]);

#ifdef AV_LOG_MORE_VERBOSE
    av_log(s->avctx, AV_LOG_MORE_VERBOSE, "input video format:\n");
    av_log(s->avctx, AV_LOG_MORE_VERBOSE, "  width         = %d\n", s->width);
    av_log(s->avctx, AV_LOG_MORE_VERBOSE, "  height        = %d\n", s->height);
    av_log(s->avctx, AV_LOG_MORE_VERBOSE, "  scanning      = %s\n", s->interlaced ? "interlaced" : "progressive");

    av_log(s->avctx, AV_LOG_MORE_VERBOSE, "luminance quants:\n");

    for (i = 0; i < 64; i++) {
        av_log(s->avctx, AV_LOG_MORE_VERBOSE, "%3d, ", s->lum_quants[i]);
        if (i % 8 == 7)
            av_log(s->avctx, AV_LOG_MORE_VERBOSE, "\n");
    }

    av_log(s->avctx, AV_LOG_MORE_VERBOSE, "chrominance quants:\n");
    for (i = 0; i < 64; i++) {
        av_log(s->avctx, AV_LOG_MORE_VERBOSE, "%3d, ", s->chrom_quants[i]);
        if (i % 8 == 7)
            av_log(s->avctx, AV_LOG_MORE_VERBOSE, "\n");
    }
#endif

    s->avctx->pix_fmt = AV_PIX_FMT_YUV422P;

    avcodec_set_dimensions(s->avctx, s->width, s->height);

    if (s->interlaced) {
        s->nb_blocks = 4;
        s->avctx->time_base = earthsoftdv_framerate_interlaced;
    }
    else {
        s->nb_blocks = 2;
        s->avctx->time_base = earthsoftdv_framerate_progressive;
    }

    /* init thread contexts */
    for (i = 0; i < s->nb_blocks; i++) {
        bc = av_mallocz(sizeof(EarthsoftDVDecodeBlockContext));

        if (!bc) {
            av_log(s->avctx, AV_LOG_ERROR, "block context allocation failed\n");
            goto failed;
        }

        bc->block_index    = i;
        bc->picture        = &s->picture;
        bc->interlaced     = s->interlaced;
        bc->width          = s->width;
        bc->height         = s->height;
        bc->lum_quants     = s->lum_quants;
        bc->chrom_quants   = s->chrom_quants;
        bc->scantable      = s->scantable;
        bc->mb_bottom_y    = -1;

        memcpy(&bc->dsp, &s->dsp, sizeof(s->dsp));

        s->block_context[i] = bc;
    }

    for (i = s->nb_blocks; i < 4; i++) {
        s->block_context[i] = NULL;
    }

    if (s->height % 16 != 0) {
        /* 32x8 dct block */
        s->block_context[s->nb_blocks - 1]->mb_bottom_y = s->height / 16;
    }

    /*
     * calculate number of macro block and arrangement locations
     * for each blocks.
     */
    nb_mb_total = (s->width * s->height) / (16 * 16);
    nb_mb_per_line = s->width / 16;

    for (i = 0; i < s->nb_blocks; i++) {
        s->block_context[i]->nb_mb_per_line = nb_mb_per_line;
        s->block_context[i]->nb_mb          = nb_mb_total / s->nb_blocks;
    }

    switch (nb_mb_total % s->nb_blocks) {
    case 1:
        s->block_context[1]->nb_mb++;
        break;
    case 2:
        s->block_context[1]->nb_mb++;
        s->block_context[3]->nb_mb++;
        break;
    case 3:
        s->block_context[1]->nb_mb++;
        s->block_context[2]->nb_mb++;
        s->block_context[3]->nb_mb++;
        break;
    default:
        break;
    }

    mb_pad_start_y = (s->height / (16 * s->nb_blocks)) * s->nb_blocks;
    nb_mb_pad = 0;

    for (i = 0; i < s->nb_blocks; i++) {
        if (i == 0) {
            s->block_context[i]->mb_pad_x = 0;
            s->block_context[i]->mb_pad_y = mb_pad_start_y;
        }
        else {
            s->block_context[i]->mb_pad_x = nb_mb_pad % nb_mb_per_line;
            s->block_context[i]->mb_pad_y = nb_mb_pad / nb_mb_per_line + mb_pad_start_y;
        }

        nb_mb_pad += s->block_context[i]->nb_mb
                     - (mb_pad_start_y / s->nb_blocks) * nb_mb_per_line;
    }

    return 0;

failed:
    esdv_decode_free(s);

    return -1;
}

static av_always_inline int esdv_decode_dctblock(EarthsoftDVDecodeBlockContext *s, GetBitContext *gb, int16_t *block, int16_t *quant_matrix)
{
    const int dc = get_sbits(gb, 9);
    const int q  = get_bits(gb, 1);
    const int ac_scale = (5 - q) - 2;
    int i;

#ifdef AV_LOG_MORE_VERBOSE
    av_log(s->avctx, AV_LOG_MORE_VERBOSE, "      dc: %d\n", dc);
    av_log(s->avctx, AV_LOG_MORE_VERBOSE, "       q: %d\n", q);
#endif

    /* DC coef */
    /* convert to unsigned because 128 is not added in the
       standard IDCT */
    block[0] = (dc << 2) + 1024;

    /* AC coefs */
    {
        int run, level, len;
        int bits;

        OPEN_READER(re, gb);

        for (i = 1;;) {
            /* decode VLC */
            {
                VLCNode *current = vlc_tree;
                int mask = 1 << VLC_BITS;

                UPDATE_CACHE(re, gb);

                bits = SHOW_UBITS(re, gb, VLC_BITS + 1 /* +1 means sign bit */);

                for (len = 0;;) {
                    if (bits & mask)
                        current = current->one;
                    else
                        current = current->zero;

                    if (!current) {
                        CLOSE_READER(re, gb);

                        av_log(s->avctx, AV_LOG_ERROR, "      invalid code: ac=%d, bits=%04x\n", i, bits);
                        return -1;
                    }

                    mask >>= 1;
                    len++;

                    if (!current->zero && !current->one)
                        break;
                }

                if (current->bits == 13) {
                    /*
                     * a pair of (run, 0)
                     * leading 7 bits(1111110b) are codeword for (run, 0),
                     * and following 6 bits are binary notation of run (6 to 61).
                     * leading 7 bits ware already read, so read following 6 bits.
                     */
                    len   = 13;
                    level = 0;
                    run   = (bits >> (VLC_BITS + 1 - 13)) & 0x3f;
                }
                else if (current->bits == 15) {
                    /*
                     * a pair of (0, level)
                     * leading 7 bits(1111111b) are codeword for (0, level),
                     * and following 8 bits are binary notation of run (23 to 255).
                     * leading 7 bits ware already read, so read following 8 bits and
                     * sign bit.
                     */
                    len   = 16; // 15 + 1
                    run   = 0;
                    level = (bits >> (VLC_BITS + 1 - 16)) & 0x1ff;
                    level = (level & 1) ? -(level >> 1) : +(level >> 1);
                }
                else {
                    run = current->run;

                    if (current->level) {
                        /* sign bit */
                        level = (bits & mask) ? -current->level : current->level;
                        len += 1;
                    }
                    else {
                        level = current->level;
                    }
                }

                LAST_SKIP_BITS(re, gb, len);
            }

#ifdef AV_LOG_MORE_VERBOSE
            if (len == 4 && level == 0)
                av_log(s->avctx, AV_LOG_MORE_VERBOSE, "      code=%04x, eob\n", bits >> (VLC_BITS + 1 - len));
            else
                av_log(s->avctx, AV_LOG_MORE_VERBOSE, "      code=%04x, len=%d, run=%d, level=%d\n", bits >> (VLC_BITS + 1 - len), len, run, level);
#endif

            if (len == 4 && level == 0)
                break; // EOB

            i += run;

            if (64 <= i) {
                av_log(s->avctx, AV_LOG_ERROR, "      run length error: ac=%d, code=%04x, len=%d, level=%d run=%d\n", i - run, bits >> (VLC_BITS + 1 - len), len, level, run);
                return -1;
            }

            block[s->scantable[i]] = (level * quant_matrix[i]) >> ac_scale;

            i++;
        }

        CLOSE_READER(re, gb);
    }

#ifdef AV_LOG_MORE_VERBOSE
    for (i = 0; i < 64; i++) {
        if (i % 8 == 0)
            av_log(s->avctx, AV_LOG_MORE_VERBOSE, "        ");
        av_log(s->avctx, AV_LOG_MORE_VERBOSE, "%6d ", block[i]);
        if (i % 8 == 7)
            av_log(s->avctx, AV_LOG_MORE_VERBOSE, "\n");
    }
#endif

    return 0;
}

static int esdv_decode_macroblock(EarthsoftDVDecodeBlockContext *s, GetBitContext *gb, int mb_x, int mb_y)
{
    /*
     * arrangement of Y0-Y3 DCT block:
     *   [Y0] [Y2]
     *   [Y1] [Y3]
     */
    int lum_put_x[4]   = {0, 0, 8, 8};
    int lum_put_y[4]   = {0, 8, 0, 8};
    /*
     * arrangement of Cr0-Cr1 DCT block:
     *   [Cr0]
     *   [Cr1]
     *
     * arrangement of Cb0-Cb1 DCT block:
     *   [Cb0]
     *   [Cb1]
     */
    int chrom_put_x[4] = {0, 0, 0, 0};
    int chrom_put_y[4] = {0, 8, 0, 8};

    LOCAL_ALIGNED_16(int16_t, dctblock, [64]);
    int y_stride[3] = {s->picture->linesize[0], s->picture->linesize[1], s->picture->linesize[2]};
    int ret;
    int i, j;

    if (s->interlaced) {
        int fieldmode = get_bits1(gb); // 0: frame mode, 1: field mode
#ifdef AV_LOG_MORE_VERBOSE
        av_log(s->avctx, AV_LOG_MORE_VERBOSE, "    dct mode: %d\n", fieldmode);
#endif
        if (fieldmode) {
            /* rearrangement of pixels in the field mode:
             *   DCT block 1 (Y0, Y2, Cb0, Cr0)         Rearranged DCT block 1
             *     A00 A01 A02 A03 A04 A05 A06 A07        A00 A01 A02 A03 A04 A05 A06 A07
             *     A10 A11 A12 A13 A14 A15 A16 A17        A20 A21 A22 A23 A24 A25 A26 A27
             *     A20 A21 A22 A23 A24 A25 A26 A27        A40 A41 A42 A43 A44 A45 A46 A47
             *     A30 A31 A32 A33 A34 A35 A36 A37        A60 A61 A62 A63 A64 A65 A66 A67
             *     A40 A41 A42 A43 A44 A45 A46 A47   =>   B00 B01 B02 B03 B04 B05 B06 B07
             *     A50 A51 A52 A53 A54 A55 A56 A57        B20 B21 B22 B23 B24 B25 B26 B27
             *     A60 A61 A62 A63 A64 A65 A66 A67        B40 B41 B42 B43 B44 B45 B46 B47
             *     A70 A71 A72 A73 A74 A75 A76 A77        B60 B61 B62 B63 B64 B65 B66 B67
             *
             *   DCT block 2 (Y1, Y3, Cb1, Cr1)         Rearranged DCT block 1
             *     B00 B01 B02 B03 B04 B05 B06 B07        A10 A11 A12 A13 A14 A15 A16 A17
             *     B10 B11 B12 B13 B14 B15 B16 B17        A30 A31 A32 A33 A34 A35 A36 A37
             *     B20 B21 B22 B23 B24 B25 B26 B27        A50 A51 A52 A53 A54 A55 A56 A57
             *     B30 B31 B32 B33 B34 B35 B36 B37        A70 A71 A72 A73 A74 A75 A76 A77
             *     B40 B41 B42 B43 B44 B45 B46 B47   =>   B10 B11 B12 B13 B14 B15 B16 B17
             *     B50 B51 B52 B53 B54 B55 B56 B57        B30 B31 B32 B33 B34 B35 B36 B37
             *     B60 B61 B62 B63 B64 B65 B66 B67        B50 B51 B52 B53 B54 B55 B56 B57
             *     B70 B71 B72 B73 B74 B75 B76 B77        B70 B71 B72 B73 B74 B75 B76 B77
             */
            lum_put_y  [1] = 1;
            lum_put_y  [3] = 1;
            chrom_put_y[1] = 1;
            chrom_put_y[3] = 1;

            for (i = 0; i < 3; i++) {
                y_stride[i] <<= 1;
            }
        }
        else if (s->mb_bottom_y == mb_y) {
            /*
             * DCT blocks at bottom-most
             * each macro blocks has 32x8 pixels.
             */
            mb_x <<= 1; // * 2

            /*
             * arrangement of Y0-Y3 DCT block at bottom-most:
             *   [Y0] [Y2] [Y1] [Y3]
             */
            lum_put_x[0] =  0;
            lum_put_x[1] = 16;
            lum_put_x[2] =  8;
            lum_put_x[3] = 24;

            /*
             * arrangement of Cr0-Cr1 DCT block at bottom-most:
             *   [Cr0] [Cr1]
             */
            chrom_put_x[0] = 0;
            chrom_put_x[1] = 8;

            /*
             * arrangement of Cb0-Cb1 DCT block at bottom-most:
             *   [Cb0] [Cb1]
             */
            chrom_put_x[2] = 0;
            chrom_put_x[3] = 8;

            for (i = 0; i < 4; i++) {
                lum_put_y  [i] = 0;
                chrom_put_y[i] = 0;
            }
        }
    }

    mb_x <<= 4; // * 16
    mb_y <<= 4; // * 16

    /* decode luminance (Y0, Y1, Y2, Y3) DCT block */
    for (i = 0; i < 4; i++) {
#ifdef AV_LOG_MORE_VERBOSE
        av_log(s->avctx, AV_LOG_MORE_VERBOSE, "    dct block: Y%d\n", i);
#endif

        s->dsp.clear_block(dctblock);

        ret = esdv_decode_dctblock(s, gb, dctblock, s->lum_quants);

        if (ret < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "    error at dct block Y%d\n", i);
            return ret;
        }

        /* put luminance DCT block */
        s->dsp.idct_put(s->picture->data[0] + (mb_x + lum_put_x[i]) + (mb_y + lum_put_y[i]) * s->picture->linesize[0], y_stride[0], dctblock);

        /*
         * inverse DCT specified in SMPTE 370M-2006:
         *
         *           7   7                  {v(2y+1)   }    {u(2x+1)   }
         * P(x,y) =  S   S  Cv Cu C(u,v) cos{------- pi} cos{------- pi}
         *          v=0 u=0                 {  16      }    {  16      }
         *
         *              where
         *                   Cu = 0.5 / sqrt(2)  for u = 0
         *                   Cu = 0.5            for u = 1 to 7
         *                   Cv = 0.5 / sqrt(2)  for v = 0
         *                   Cv = 0.5            for v = 1 to 7
         */
    }

    /* decode chrominance (Cr0, Cr1, Cb0, Cb1) DCT block */
    mb_x >>= 1;

    for (i = 0; i < 4; i++) {
        j = (i < 2) ? 2 : 1;

#ifdef AV_LOG_MORE_VERBOSE
        av_log(s->avctx, AV_LOG_MORE_VERBOSE, "    dct block: C%d\n", i);
#endif

        s->dsp.clear_block(dctblock);

        ret = esdv_decode_dctblock(s, gb, dctblock, s->chrom_quants);

        if (ret < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "    error at dct block C%d\n", i);
            return ret;
        }

        /* put chrominance DCT block */
        s->dsp.idct_put(s->picture->data[j] + (mb_x + chrom_put_x[i]) + (mb_y + chrom_put_y[i]) * s->picture->linesize[j], y_stride[j], dctblock);
    }

    return 0;
}

static int esdv_decode_block_thread(AVCodecContext *c, void *arg) {
    EarthsoftDVDecodeBlockContext *s = *(void**)arg;
    int mb_x = 0;
    int mb_y = s->block_index;
    int mb_y_step = (s->interlaced ? 4 : 2);
    int mb_pad = 0;
    int mb;
    int ret;

    s->avctx = c;

    for (mb = 0; mb < s->nb_mb; mb++) {
#ifdef AV_LOG_MORE_VERBOSE
        av_log(s->avctx, AV_LOG_MORE_VERBOSE, "  macro block (%d,%d)\n", mb_x, mb_y);
#endif

        ret = esdv_decode_macroblock(s, &s->gb, mb_x, mb_y);

        if (ret < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "  error at frame %d, macro block (%d,%d), video block %d\n", s->nb_frames, mb_x, mb_y, s->block_index);
            continue;
        }

        mb_x++;

        if (mb_x == s->nb_mb_per_line) {
            mb_x  = 0;
            mb_y += mb_y_step;

            if (0 < s->block_index && !mb_pad && s->mb_pad_y <= mb_y) {
                mb_pad    = 1;
                mb_y_step = 1;
                mb_y      = s->mb_pad_y;
                mb_x      = s->mb_pad_x;
            }
        }
    }

    return 0;
}

static int esdv_decode_frame(AVCodecContext *avctx,
                               void *data, int *got_frame,
                               AVPacket *avpkt)
{
    EarthsoftDVDecodeContext *s = avctx->priv_data;
    EarthsoftDVVideoContext *video = avpkt->data;
    int ret;
    int block;

    if (!s->initialized) {
        ret = esdv_decode_init_context(s, video);

        if (ret < 0)
            return ret;

        s->initialized = 1;
    }

    if (s->picture.data[0])
        avctx->release_buffer(avctx, &s->picture);

    s->picture.reference = 0;

    if (ff_get_buffer(avctx, &s->picture) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    s->picture.interlaced_frame = s->interlaced;

    // 1080i is top field first, 480i is top field second
    s->picture.top_field_first  = (s->height == 1080);

    for (block = 0; block < s->nb_blocks; block++) {
        s->block_context[block]->nb_frames = s->nb_frames;
        init_get_bits(&s->block_context[block]->gb, video->block.buffer[block], video->block.size[block] * 8);
    }

    s->avctx->execute(s->avctx, esdv_decode_block_thread, (void**)&(s->block_context[0]), NULL, s->nb_blocks, sizeof(void*));
    s->nb_frames++;

    emms_c();

    /* return image */
    *got_frame = 1;
    *(AVFrame*)data = s->picture;

    return 0;
}

static int esdv_decode_close(AVCodecContext *c)
{
    EarthsoftDVDecodeContext *s = c->priv_data;

    if (s->picture.data[0])
        c->release_buffer(c, &s->picture);

    esdv_decode_free(s);

    return 0;
}

AVCodec ff_earthsoftdv_decoder = {
    .name           = "earthsoftdv",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_EARTHSOFTDV,
    .priv_data_size = sizeof(EarthsoftDVDecodeContext),
    .init           = esdv_decode_init,
    .close          = esdv_decode_close,
    .decode         = esdv_decode_frame,
    .capabilities   = CODEC_CAP_DR1 | CODEC_CAP_SLICE_THREADS,
    .long_name      = NULL_IF_CONFIG_SMALL("Earthsoft PV3/PV4 DV video codec"),
};
#endif /* if CONFIG_EARTHSOFTDV_DECODER */
