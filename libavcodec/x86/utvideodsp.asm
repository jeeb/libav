;******************************************************************************
;* x86-SIMD-optimized X for utvideo
;*
;* Copyright (c) 2012 Jan Ekström <jeebjp@gmail.com>
;*
;* This file is part of Libav.
;*
;* Libav is free software; you can redistribute it and/or
;* modify it under the terms of the GNU Lesser General Public
;* License as published by the Free Software Foundation; either
;* version 2.1 of the License, or (at your option) any later version.
;*
;* Libav is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;* Lesser General Public License for more details.
;*
;* You should have received a copy of the GNU Lesser General Public
;* License along with Libav; if not, write to the Free Software
;* 51, Inc., Foundation Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "x86inc.asm"
%include "x86util.asm"

SECTION_RODATA

pb_128: times 4 db 0x80

section .text

;-----------------------------------------------------------------------------
; restore_median_slice(uint8_t *src, int step, int stride, int width,
;                      int slice_start, int slice_height)
; %1 = nr. of xmm registers used
;-----------------------------------------------------------------------------
%macro RESTORE_MEDIAN_SLICE 1
cglobal restore_median_slice, 7, 7, %1, src, dst, step, stride, width, slice_start, slice_height
    movd           r0, stride
    imul  slice_start, r0
    add           src, slice_start
    movd           m0, [src]
    movd           m1, [pb_128]
    paddb          m0, m1
    movd        [src], m0
    add           src, step
.loop
    movd           m1, [src]
    paddb          m0, m1
    movd        [src], m0
    add           src, step
    jmp .loop
    RET
%endmacro
