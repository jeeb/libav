/*
 * Common Ut Video header
 * Copyright (c) 2011 Konstantin Shishkov
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_UTVIDEO_H
#define AVCODEC_UTVIDEO_H

/**
 * @file
 * Common Ut Video header
 */

#include "libavutil/common.h"
#include "avcodec.h"
#include "dsputil.h"

enum {
    PRED_NONE = 0,
    PRED_LEFT,
    PRED_GRADIENT,
    PRED_MEDIAN,
};

enum {
    COMP_NONE = 0,
    COMP_HUFF,
};

/* Based on values gotten from the official encoder. */
enum {
    UTVIDEO_RGB  = MKTAG(0x00, 0x00, 0x01, 0x18),
    UTVIDEO_RGBA = MKTAG(0x00, 0x00, 0x02, 0x18),
    UTVIDEO_420  = MKTAG('Y', 'V', '1', '2'),
    UTVIDEO_422  = MKTAG('Y', 'U', 'Y', '2'),
};

static const int pred_order[5] = {
    PRED_LEFT, PRED_MEDIAN, PRED_MEDIAN, PRED_NONE, PRED_GRADIENT
};

static const int rgb_order[4]  = { 1, 2, 0, 3 }; // G, B, R, A

typedef struct UtvideoContext {
    AVCodecContext *avctx;
    AVFrame        pic;
    DSPContext     dsp;

    uint32_t frame_info_size, flags, frame_info;
    int      planes;
    int      slices;
    int      compression;
    int      interlaced;
    int      frame_pred;

    uint8_t *slice_bits;
    int     slice_bits_size;
} UtvideoContext;

typedef struct HuffEntry {
    uint8_t  sym;
    uint8_t  len;
    uint32_t code;
} HuffEntry;

/* Compares huffentries' lengths */
static int huff_cmp_len(const void *a, const void *b)
{
    const HuffEntry *aa = a, *bb = b;
    return (aa->len - bb->len)*256 + aa->sym - bb->sym;
}

#endif /* AVCODEC_UTVIDEO_H */
