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

pd_80_3: dd 0x00808080
pd_80_4: dd 0x80808080

section .text

;-----------------------------------------------------------------------------
; restore_median_slice(uint8_t *src, int step, int stride, int width,
;                      int slice_start, int slice_height)
; %1 = nr. of xmm registers used
;-----------------------------------------------------------------------------
%macro RESTORE_MEDIAN_SLICE 1
cglobal restore_median_slice, 6, 6, %1, src, step, stride, width, slice_start, slice_height
    imul slice_start, stride
    add          src, slice_start
    movd          m0, [src]
    movd          m1, [pd_80_3]
    paddb         m0, m1
    RET
%endmacro