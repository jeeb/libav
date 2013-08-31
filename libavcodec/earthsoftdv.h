/*
 * Constants for Earthsoft PV3/PV4 DV codec
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

/**
 * @file libavcodec/earthsoftdv.h
 * Constants for Earthsoft PV3/PV4 DV codec.
 */

#ifndef AVCODEC_EARTHSOFTDV_H
#define AVCODEC_EARTHSOFTDV_H

#include "libavutil/rational.h"
#include "avcodec.h"

// FIXME: how large of block size is possible?
#define EARTHSOFTDV_MAX_VIDEO_BLOCK_SIZE  72 * 4096
#define EARTHSOFTDV_MAX_AUDIO_SAMPLE_RATE 48000

#define EARTHSOFTDV_MAX_AUDIO_FRAME_PER_BLOCK \
        (1 + ((EARTHSOFTDV_MAX_AUDIO_SAMPLE_RATE * 1001) / 30000))
#define EARTHSOFTDV_MAX_AUDIO_BLOCK_SIZE \
        (EARTHSOFTDV_MAX_AUDIO_FRAME_PER_BLOCK * 2 * 2) // 16bit, 2ch

typedef struct EarthsoftDVVideoBlock {
    AVRational dar;
    uint8_t encoding_q;
    uint32_t size[4];
    uint8_t buffer[4][EARTHSOFTDV_MAX_VIDEO_BLOCK_SIZE];
} EarthsoftDVVideoBlock;

typedef struct EarthsoftDVVideoContext {
    uint8_t codec_version;
    int interlaced;
    int width, height;
    int16_t lum_quants[64];
    int16_t chrom_quants[64];
    EarthsoftDVVideoBlock block;
} EarthsoftDVVideoContext;

typedef struct EarthsoftDVAudioBlock {
    uint64_t accum_frame_count;
    uint16_t frame_count;
    uint32_t sample_rate;
    uint8_t buffer[EARTHSOFTDV_MAX_AUDIO_BLOCK_SIZE];
} EarthsoftDVAudioBlock;

typedef struct EarthsoftDVAudioContext {
    uint64_t accum_frame_count;
    uint32_t sample_rate;
    EarthsoftDVAudioBlock block;
} EarthsoftDVAudioContext;

static const AVRational earthsoftdv_framerate_interlaced  = {1001, 30000};
static const AVRational earthsoftdv_framerate_progressive = {1001, 60000};

#endif /* AVCODEC_EARTHSOFTDV_H */
