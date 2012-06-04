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

#include "libavcodec/utvideodsp.h"
#include "libavutil/cpu.h"

void restore_median_slice(uint8_t *src, uint8_t *dst, int step, int stride, int width,
                          int slice_start, int slice_height);

void ff_utvideodsp_x86_init(UtvideoDSPContext *dsp)
{
#if HAVE_YASM
    int flags = av_get_cpu_flags();

    dsp->restore_median_slice = restore_median_slice;
#endif /* HAVE_YASM */
}
