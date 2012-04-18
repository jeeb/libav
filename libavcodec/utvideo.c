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

/**
 * @file
 * Ut Video decoder
 */

#include <stdlib.h>

#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "bytestream.h"
#include "get_bits.h"
#include "dsputil.h"

enum {
    PRED_NONE = 0,
    PRED_LEFT,
    PRED_GRADIENT,
    PRED_MEDIAN,
};

typedef struct UtvideoThreadData {
    const uint8_t *src;
    uint8_t *dst;

    int width;
    int height;
    int stride;
    int plane_no;

    int rmode;
    int fsym;
    int cmask;
    int step;
    VLC *vlc;
    int use_pred;
} UtvideoThreadData;

typedef struct UtvideoContext {
    AVCodecContext *avctx;
    AVFrame pic;
    DSPContext dsp;

    uint32_t frame_info_size, flags, frame_info;
    int planes;
    int slices;
    int compression;
    int interlaced;
    int frame_pred;

    uint8_t **slice_bits;
    int *slice_bits_size;
} UtvideoContext;

typedef struct HuffEntry {
    uint8_t sym;
    uint8_t len;
} HuffEntry;

static int huff_cmp(const void *a, const void *b)
{
    const HuffEntry *aa = a, *bb = b;
    return (aa->len - bb->len)*256 + aa->sym - bb->sym;
}

static int build_huff(const uint8_t *src, VLC *vlc, int *fsym)
{
    int i;
    HuffEntry he[256];
    int last;
    uint32_t codes[256];
    uint8_t bits[256];
    uint8_t syms[256];
    uint32_t code;

    *fsym = -1;
    for (i = 0; i < 256; i++) {
        he[i].sym = i;
        he[i].len = *src++;
    }
    qsort(he, 256, sizeof(*he), huff_cmp);

    if (!he[0].len) {
        *fsym = he[0].sym;
        return 0;
    }
    if (he[0].len > 32)
        return -1;

    last = 255;
    while (he[last].len == 255 && last)
        last--;

    code = 1;
    for (i = last; i >= 0; i--) {
        codes[i] = code >> (32 - he[i].len);
        bits[i]  = he[i].len;
        syms[i]  = he[i].sym;
        code += 0x80000000u >> (he[i].len - 1);
    }

    return ff_init_vlc_sparse(vlc, FFMIN(he[last].len, 9), last + 1,
                              bits,  sizeof(*bits),  sizeof(*bits),
                              codes, sizeof(*codes), sizeof(*codes),
                              syms,  sizeof(*syms),  sizeof(*syms), 0);
}

static int decode_slice(AVCodecContext *avctx, void *tdata, int jobnr,
                        int threadnr)
{
    UtvideoContext * const c = avctx->priv_data;
    UtvideoThreadData *td = tdata;
    GetBitContext gb;

    int i, j, pix;
    int start = jobnr ? (td->height * jobnr / c->slices) & td->cmask : 0;
    int end   = (td->height * (jobnr + 1) / c->slices) & td->cmask;
    int prev  = 0x80;

    uint8_t *slice_bits = c->slice_bits[jobnr];

    uint8_t *dest;
    int slice_data_start, slice_data_end, slice_size;

    dest = td->dst + start * td->stride;

    if (td->fsym >= 0) { // build_huff reported a symbol to fill slices with
        for (j = start; j < end; j++) {
            for (i = 0; i < td->width * td->step; i += td->step) {
                pix = td->fsym;
                if (td->use_pred) {
                    prev += pix;
                    pix   = prev;
                }
                dest[i] = pix;
            }
            dest += td->stride;
        }
        return 0;
    }

    // slice offset and size validation was done earlier
    slice_data_start = jobnr ? AV_RL32(td->src + jobnr * 4 - 4) : 0;
    slice_data_end   = AV_RL32(td->src + jobnr * 4);
    slice_size       = slice_data_end - slice_data_start;

    if (!slice_size) {
        for (j = start; j < end; j++) {
            for (i = 0; i < td->width * td->step; i += td->step)
                dest[i] = 0x80;
            dest += td->stride;
        }
        return 0;
    }

    memcpy(slice_bits, td->src + slice_data_start + c->slices * 4, slice_size);
    memset(slice_bits + slice_size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
    c->dsp.bswap_buf((uint32_t *) slice_bits, (uint32_t *) slice_bits,
                     (slice_data_end - slice_data_start + 3) >> 2);
    init_get_bits(&gb, slice_bits, slice_size * 8);

    for (j = start; j < end; j++) {
        for (i = 0; i < td->width * td->step; i += td->step) {
            if (get_bits_left(&gb) <= 0) {
                av_log(c->avctx, AV_LOG_ERROR,
                       "Slice decoding ran out of bits\n");
                goto fail_slice;
            }
            pix = get_vlc2(&gb, td->vlc->table, td->vlc->bits, 4);
            if (pix < 0) {
                av_log(c->avctx, AV_LOG_ERROR, "Decoding error\n");
                goto fail_slice;
            }
            if (td->use_pred) {
                prev += pix;
                pix   = prev;
            }
            dest[i] = pix;
        }
        dest += td->stride;
    }
    if (get_bits_left(&gb) > 32)
        av_log(c->avctx, AV_LOG_WARNING, "%d bits left after decoding slice\n",
               get_bits_left(&gb));

    return 0;
fail_slice:
    return AVERROR_INVALIDDATA;
}

static const int rgb_order[4] = { 1, 2, 0, 3 };

static void restore_rgb_planes(uint8_t *src, int step, int stride, int width,
                               int height)
{
    int i, j;
    uint8_t r, g, b;

    for (j = 0; j < height; j++) {
        for (i = 0; i < width * step; i += step) {
            r = src[i];
            g = src[i + 1];
            b = src[i + 2];
            src[i]     = r + g - 0x80;
            src[i + 2] = b + g - 0x80;
        }
        src += stride;
    }
}

static int restore_median_slice(AVCodecContext *avctx, void *tdata, int jobnr,
                                int threadnr)
{
    UtvideoThreadData *td    = tdata;
    UtvideoContext * const c = avctx->priv_data;

    int i, j;
    int A, B, C;
    uint8_t *bsrc;
    int slice_start, slice_height;

    slice_start  = ((jobnr * td->height) / c->slices) & td->cmask;
    slice_height = ((((jobnr + 1) * td->height) / c->slices) & td->cmask)
                   - slice_start;

    bsrc = td->dst + slice_start * td->stride;

    // first line - left neighbour prediction
    bsrc[0] += 0x80;
    A = bsrc[0];
    for (i = td->step; i < td->width * td->step; i += td->step) {
        bsrc[i] += A;
        A = bsrc[i];
    }
    bsrc += td->stride;
    if (slice_height == 1)
        return 0;
    // second line - first element has top predition, the rest uses median
    C = bsrc[-td->stride];
    bsrc[0] += C;
    A = bsrc[0];
    for (i = td->step; i < td->width * td->step; i += td->step) {
        B = bsrc[i - td->stride];
        bsrc[i] += mid_pred(A, B, (uint8_t)(A + B - C));
        C = B;
        A = bsrc[i];
    }
    bsrc += td->stride;
    // the rest of lines use continuous median prediction
    for (j = 2; j < slice_height; j++) {
        for (i = 0; i < td->width * td->step; i += td->step) {
            B = bsrc[i - td->stride];
            bsrc[i] += mid_pred(A, B, (uint8_t)(A + B - C));
            C = B;
            A = bsrc[i];
        }
        bsrc += td->stride;
    }
    return 0;
}

/* UtVideo interlaced mode treats every two lines as a single one,
 * so restoring function should take care of possible padding between
 * two parts of the same "line".
 */
static int restore_median_slice_il(AVCodecContext *avctx, void *tdata,
                                   int jobnr, int threadnr)
{
    UtvideoThreadData *td = tdata;
    UtvideoContext * const c = avctx->priv_data;

    int i, j;
    int A, B, C;
    uint8_t *bsrc;
    int slice_start, slice_height;

    const int cmask   = ~(td->rmode ? 3 : 1);
    const int stride2 = td->stride << 1;

    slice_start    = ((jobnr * td->height) / c->slices) & cmask;
    slice_height   = ((((jobnr + 1) * td->height) / c->slices) & cmask)
                     - slice_start;
    slice_height >>= 1;

    bsrc = td->dst + slice_start * td->stride;

    // first line - left neighbour prediction
    bsrc[0] += 0x80;
    A = bsrc[0];
    for (i = td->step; i < td->width * td->step; i += td->step) {
        bsrc[i] += A;
        A = bsrc[i];
    }
    for (i = 0; i < td->width * td->step; i += td->step) {
        bsrc[td->stride + i] += A;
        A = bsrc[td->stride + i];
    }
    bsrc += stride2;
    if (slice_height == 1)
        return 0;
    // second line - first element has top prediction, the rest uses median
    C = bsrc[-stride2];
    bsrc[0] += C;
    A = bsrc[0];
    for (i = td->step; i < td->width * td->step; i += td->step) {
        B = bsrc[i - stride2];
        bsrc[i] += mid_pred(A, B, (uint8_t)(A + B - C));
        C = B;
        A = bsrc[i];
    }
    for (i = 0; i < td->width * td->step; i += td->step) {
        B = bsrc[i - td->stride];
        bsrc[td->stride + i] += mid_pred(A, B, (uint8_t)(A + B - C));
        C = B;
        A = bsrc[td->stride + i];
    }
    bsrc += stride2;
    // the rest of lines use continuous median prediction
    for (j = 2; j < slice_height; j++) {
        for (i = 0; i < td->width * td->step; i += td->step) {
            B = bsrc[i - stride2];
            bsrc[i] += mid_pred(A, B, (uint8_t)(A + B - C));
            C = B;
            A = bsrc[i];
        }
        for (i = 0; i < td->width * td->step; i += td->step) {
            B = bsrc[i - td->stride];
            bsrc[td->stride + i] += mid_pred(A, B, (uint8_t)(A + B - C));
            C = B;
            A = bsrc[td->stride + i];
        }
        bsrc += stride2;
    }
    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *data_size,
                        AVPacket *avpkt)
{
    UtvideoContext *c = avctx->priv_data;
    UtvideoThreadData tdata;
    VLC vlc;

    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    int i, j;
    const uint8_t *plane_start[5];
    int plane_size, max_slice_size = 0, slice_start, slice_end, slice_size;
    int ret;
    GetByteContext gb;

    if (c->pic.data[0])
        avctx->release_buffer(avctx, &c->pic);

    c->pic.reference = 1;
    c->pic.buffer_hints = FF_BUFFER_HINTS_VALID;
    if ((ret = avctx->get_buffer(avctx, &c->pic)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }

    /* parse plane structure to retrieve frame flags and validate slice offsets */
    bytestream2_init(&gb, buf, buf_size);
    for (i = 0; i < c->planes; i++) {
        plane_start[i] = gb.buffer;
        if (bytestream2_get_bytes_left(&gb) < 256 + 4 * c->slices) {
            av_log(avctx, AV_LOG_ERROR, "Insufficient data for a plane\n");
            return AVERROR_INVALIDDATA;
        }
        bytestream2_skipu(&gb, 256);
        slice_start = 0;
        slice_end   = 0;
        for (j = 0; j < c->slices; j++) {
            slice_end   = bytestream2_get_le32u(&gb);
            slice_size  = slice_end - slice_start;
            if (slice_end <= 0 || slice_size <= 0 ||
                bytestream2_get_bytes_left(&gb) < slice_end) {
                av_log(avctx, AV_LOG_ERROR, "Incorrect slice size\n");
                return AVERROR_INVALIDDATA;
            }
            slice_start = slice_end;
            max_slice_size = FFMAX(max_slice_size, slice_size);
        }
        plane_size = slice_end;
        bytestream2_skipu(&gb, plane_size);
    }
    plane_start[c->planes] = gb.buffer;
    if (bytestream2_get_bytes_left(&gb) < c->frame_info_size) {
        av_log(avctx, AV_LOG_ERROR, "Not enough data for frame information\n");
        return AVERROR_INVALIDDATA;
    }
    c->frame_info = bytestream2_get_le32u(&gb);
    av_log(avctx, AV_LOG_DEBUG, "frame information flags %X\n", c->frame_info);

    c->frame_pred = (c->frame_info >> 8) & 3;

    if (c->frame_pred == PRED_GRADIENT) {
        av_log_ask_for_sample(avctx, "Frame uses gradient prediction\n");
        return AVERROR_PATCHWELCOME;
    }

    for(i = 0; i < c->slices; i++) {
        av_fast_malloc(&c->slice_bits[i], &c->slice_bits_size[i],
                       max_slice_size + FF_INPUT_BUFFER_PADDING_SIZE);

        if (!c->slice_bits[i]) {
            av_log(avctx, AV_LOG_ERROR, "Cannot allocate temporary buffer\n");
            return AVERROR(ENOMEM);
        }
    }

    tdata.width    = avctx->width;
    tdata.height   = avctx->height;
    tdata.rmode    = 0;
    tdata.cmask    = ~tdata.rmode;
    tdata.vlc      = &vlc;
    tdata.use_pred = (c->frame_pred == PRED_LEFT);

    switch (c->avctx->pix_fmt) {
    case PIX_FMT_RGB24:
    case PIX_FMT_RGBA:
        tdata.stride = c->pic.linesize[0];
        tdata.step   = c->planes;
        for (tdata.plane_no = 0; tdata.plane_no < c->planes; tdata.plane_no++) {
            tdata.src = plane_start[tdata.plane_no];
            tdata.dst = c->pic.data[0] + rgb_order[tdata.plane_no];

            if (build_huff(tdata.src, &vlc, &tdata.fsym)) {
                av_log(avctx, AV_LOG_ERROR, "Cannot build Huffman codes\n");
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }

            if (tdata.fsym < 0)
                tdata.src += 256;

            ret = avctx->execute2(avctx, decode_slice, &tdata, NULL, c->slices);
            if (ret)
                goto fail;
            if (c->frame_pred == PRED_MEDIAN)
                avctx->execute2(avctx, restore_median_slice, &tdata, NULL,
                                c->slices);
        }
        restore_rgb_planes(c->pic.data[0], c->planes, c->pic.linesize[0],
                           avctx->width, avctx->height);
        break;
    case PIX_FMT_YUV420P:
        tdata.step = 1;
        for (tdata.plane_no = 0; tdata.plane_no < 3; tdata.plane_no++) {
            tdata.src = plane_start[tdata.plane_no];
            tdata.dst = c->pic.data[tdata.plane_no];

            tdata.width  = avctx->width >> !!tdata.plane_no;
            tdata.height = avctx->height >> !!tdata.plane_no;
            tdata.stride = c->pic.linesize[tdata.plane_no];

            tdata.rmode = !tdata.plane_no;
            tdata.cmask = ~tdata.rmode;

            if (build_huff(tdata.src, &vlc, &tdata.fsym)) {
                av_log(avctx, AV_LOG_ERROR, "Cannot build Huffman codes\n");
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }

            if (tdata.fsym < 0)
                tdata.src += 256;

            ret = avctx->execute2(avctx, decode_slice, &tdata, NULL, c->slices);
            if (ret)
                goto fail;
            if (c->frame_pred == PRED_MEDIAN) {
                if (!c->interlaced) {
                    avctx->execute2(avctx, restore_median_slice, &tdata,
                                    NULL, c->slices);
                } else {
                    avctx->execute2(avctx, restore_median_slice_il, &tdata,
                                    NULL, c->slices);
                }
            }
        }
        break;
    case PIX_FMT_YUV422P:
        tdata.step = 1;
        for (tdata.plane_no = 0; tdata.plane_no < 3; tdata.plane_no++) {
            tdata.src = plane_start[tdata.plane_no];
            tdata.dst = c->pic.data[tdata.plane_no];

            tdata.width  = avctx->width  >> !!tdata.plane_no;
            tdata.stride = c->pic.linesize[tdata.plane_no];

            if (build_huff(tdata.src, &vlc, &tdata.fsym)) {
                av_log(avctx, AV_LOG_ERROR, "Cannot build Huffman codes\n");
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }

            if (tdata.fsym < 0)
                tdata.src += 256;

            ret = avctx->execute2(avctx, decode_slice, &tdata, NULL, c->slices);
            if (ret)
                goto fail;
            if (c->frame_pred == PRED_MEDIAN) {
                if (!c->interlaced) {
                    avctx->execute2(avctx, restore_median_slice, &tdata, NULL,
                                    c->slices);
                } else {
                    avctx->execute2(avctx, restore_median_slice_il, &tdata,
                                    NULL, c->slices);
                }
            }
        }
        break;
    }

    c->pic.key_frame = 1;
    c->pic.pict_type = AV_PICTURE_TYPE_I;
    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = c->pic;

    ff_free_vlc(&vlc);
    av_freep(&tdata);

    /* always report that the buffer was completely consumed */
    return buf_size;
fail:
    ff_free_vlc(&vlc);
    av_freep(&tdata);
    return ret;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    UtvideoContext * const c = avctx->priv_data;

    c->avctx = avctx;

    ff_dsputil_init(&c->dsp, avctx);

    if (avctx->extradata_size < 16) {
        av_log(avctx, AV_LOG_ERROR, "Insufficient extradata size %d, should be at least 16\n",
               avctx->extradata_size);
        return AVERROR_INVALIDDATA;
    }

    av_log(avctx, AV_LOG_DEBUG, "Encoder version %d.%d.%d.%d\n",
           avctx->extradata[3], avctx->extradata[2],
           avctx->extradata[1], avctx->extradata[0]);
    av_log(avctx, AV_LOG_DEBUG, "Original format %X\n", AV_RB32(avctx->extradata + 4));
    c->frame_info_size = AV_RL32(avctx->extradata + 8);
    c->flags           = AV_RL32(avctx->extradata + 12);

    if (c->frame_info_size != 4)
        av_log_ask_for_sample(avctx, "Frame info is not 4 bytes\n");
    av_log(avctx, AV_LOG_DEBUG, "Encoding parameters %08X\n", c->flags);
    c->slices      = (c->flags >> 24) + 1;
    c->compression = c->flags & 1;
    c->interlaced  = c->flags & 0x800;

    c->slice_bits_size = av_mallocz(sizeof(*c->slice_bits_size) * c->slices);

    if(!c->slice_bits_size) {
        av_log(avctx, AV_LOG_ERROR, "Cannot allocate temporary buffer sizes\n");
        return AVERROR(ENOMEM);
    }

    c->slice_bits = av_mallocz(sizeof(*c->slice_bits) * c->slices);

    if(!c->slice_bits) {
        av_log(avctx, AV_LOG_ERROR, "Cannot allocate temporary buffer\n");
        return AVERROR(ENOMEM);
    }

    switch (avctx->codec_tag) {
    case MKTAG('U', 'L', 'R', 'G'):
        c->planes      = 3;
        avctx->pix_fmt = PIX_FMT_RGB24;
        break;
    case MKTAG('U', 'L', 'R', 'A'):
        c->planes      = 4;
        avctx->pix_fmt = PIX_FMT_RGBA;
        break;
    case MKTAG('U', 'L', 'Y', '0'):
        c->planes      = 3;
        avctx->pix_fmt = PIX_FMT_YUV420P;
        break;
    case MKTAG('U', 'L', 'Y', '2'):
        c->planes      = 3;
        avctx->pix_fmt = PIX_FMT_YUV422P;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown Ut Video FOURCC provided (%08X)\n",
               avctx->codec_tag);
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    int i;
    UtvideoContext * const c = avctx->priv_data;

    if (c->pic.data[0])
        avctx->release_buffer(avctx, &c->pic);

    for (i = 0; i < c->slices; i++)
        av_freep(&c->slice_bits[i]);

    av_freep(&c->slice_bits);
    av_freep(&c->slice_bits_size);

    return 0;
}

AVCodec ff_utvideo_decoder = {
    .name           = "utvideo",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_UTVIDEO,
    .priv_data_size = sizeof(UtvideoContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1 | CODEC_CAP_SLICE_THREADS,
    .long_name      = NULL_IF_CONFIG_SMALL("Ut Video"),
};
