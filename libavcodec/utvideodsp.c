/*
 * Ut Video decoder
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

#include "utvideodsp.h"

static void restore_median_slice_c(uint8_t *src, int step, int stride,
                                   int width, int slice_start,
                                   int slice_height)
{
    int i, j, plane;
    int A, B, C;
    uint8_t *bsrc;

    for (plane = 0; plane < step; plane++) {
        bsrc = (src + plane) + slice_start * stride;

        // first line - left neighbour prediction
        bsrc[0] += 0x80;
        A = bsrc[0];
        for (i = step; i < width * step; i += step) {
            bsrc[i] += A;
            A        = bsrc[i];
        }
        bsrc += stride;
        if (slice_height == 1)
            continue;
        // second line - first element has top prediction, the rest uses median
        C        = bsrc[-stride];
        bsrc[0] += C;
        A        = bsrc[0];
        for (i = step; i < width * step; i += step) {
            B        = bsrc[i - stride];
            bsrc[i] += mid_pred(A, B, (uint8_t)(A + B - C));
            C        = B;
            A        = bsrc[i];
        }
        bsrc += stride;
        // the rest of lines use continuous median prediction
        for (j = 2; j < slice_height; j++) {
            for (i = 0; i < width * step; i += step) {
                B        = bsrc[i - stride];
                bsrc[i] += mid_pred(A, B, (uint8_t)(A + B - C));
                C        = B;
                A        = bsrc[i];
            }
            bsrc += stride;
        }
    }
}

void ff_utvideodsp_init(UtvideoDSPContext *dsp)
{
    dsp->restore_median_slice = restore_median_slice_c;

    ff_utvideodsp_x86_init(dsp);
}