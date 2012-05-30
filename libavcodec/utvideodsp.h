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

#ifndef AVCODEC_UTVIDEODSP_H
#define AVCODEC_UTVIDEODSP_H

#include "dsputil.h"
#include "mathops.h"

typedef struct UtvideoDSPContext {
    void (* restore_median_slice) (uint8_t *src, int step, int stride,
                             int width, int height, int slices, int rmode);
} UtvideoDSPContext;

void ff_utvideodsp_init(UtvideoDSPContext *dsp);

void ff_utvideodsp_x86_init(UtvideoDSPContext *dsp);

#endif /* AVCODEC_UTVIDEODSP_H */