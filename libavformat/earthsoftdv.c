/*
 * Earthsoft PV3/PV4 DV muxer and demuxer
 * Copyright (C) 2009-2013 smdn (http://smdn.jp/)
 *
 * This file is patch for muxing/demuxing Earthsoft PV3/PV4 DV with Libav,
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

#include "avformat.h"
#include "internal.h"
#include "libavcodec/earthsoftdv.h"
#include "earthsoftdv.h"
#include "libavutil/fifo.h"

#if CONFIG_EARTHSOFTDV_MUXER || CONFIG_EARTHSOFTDV_DEMUXER

#define AV_LOG_MORE_VERBOSE (AV_LOG_DEBUG + 1)
#undef AV_LOG_MORE_VERBOSE

#define FILE_HEADER_SIZE          16384
#define FRAME_HEADER_FIXED_SIZE   512
#define INDEX_ENTRY_SIZE          16
#define INDEX_OFFSET_SHIFT        12 // 4096

/* align must be 2^n */
#define avio_seekto_align(s, align) \
        avio_seek(s, (avio_tell(s) + (align) - 1) & (~((align) - 1)), SEEK_SET);
#define avio_skipto_align(s, skip, align) \
        avio_seek(s, (avio_tell(s) + (skip) + (align) - 1) & (~((align) - 1)), SEEK_SET);

typedef struct EarthsoftDVIndex EarthsoftDVIndex;

struct EarthsoftDVIndex {
    EarthsoftDVIndex *next;
    int frame;
    uint32_t frame_offset; // actual value >> 12
    uint16_t frame_size; // actual value >> 12
    uint64_t accum_audio_frame_count;
    uint16_t audio_frame_count;
    uint8_t encoding_q;
};

static void esdv_free_index(EarthsoftDVIndex *first)
{
    EarthsoftDVIndex *current;
    EarthsoftDVIndex *next;

    for (current = first; current != NULL;) {
        next = current->next;

        av_free(current);

        current = next;
    }
}
#endif

#if CONFIG_EARTHSOFTDV_DEMUXER

#define DEMUX_RAW_AUDIO_BUFFER_SIZE (EARTHSOFTDV_MAX_AUDIO_BLOCK_SIZE * 3)

#define DEMUX_STATISTICS
//#undef DEMUX_STATISTICS

#define READ_FRAME_HEADER   0
#define READ_AUDIO_BLOCK    1
#define READ_VIDEO_BLOCK    2

#ifdef DEMUX_STATISTICS
typedef struct DemuxStat {
    int64_t min;
    int64_t max;
    int64_t sum;
    int64_t count;
} DemuxStat;
#endif

typedef struct EarthsoftDVDemuxContext {
    AVFormatContext *fctx;
    AVStream *vst;
    AVStream *ast;
    AVPacket video_pkt;
    int read_context;
    int has_audio;
    int nonpcm_packet_size;
    int nonpcm_packet_pts;
    AVFifoBuffer *raw_audio_buffer;
    EarthsoftDVVideoContext video;
    EarthsoftDVAudioContext audio;
    int frame_current;
    EarthsoftDVIndex *index_first;
    EarthsoftDVIndex *index_last;
    EarthsoftDVIndex *index_current;
#ifdef DEMUX_STATISTICS
    DemuxStat vblock_size;
#endif
} EarthsoftDVDemuxContext;

static int esdv_read_probe(AVProbeData *p)
{
    if (p->buf[0] == 'P' &&
        p->buf[1] == 'V' &&
        p->buf[2] == '3')
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int esdv_read_index(EarthsoftDVDemuxContext *c, AVIOContext *pb)
{
    EarthsoftDVIndex *index = c->index_first;
    int nb_indices = 0;

    if (avio_size(pb) < avio_tell(pb) + INDEX_ENTRY_SIZE) {
        c->index_first = NULL;
        c->index_last = NULL;
        return 0;
    }

    c->index_first = av_malloc(sizeof(EarthsoftDVIndex));

    if (!c->index_first)
        return -1;

    index = c->index_first;

    for (;;) {
        index->frame = nb_indices;
        index->frame_offset             = avio_rb32(pb);
        index->frame_size               = avio_rb16(pb);
        index->accum_audio_frame_count  = avio_rb24(pb) << 24 | avio_rb24(pb);
        index->audio_frame_count        = avio_rb16(pb);
        index->encoding_q               = avio_r8(pb);

        avio_skip(pb, 1); // 8bit: reserved

#ifdef AV_LOG_MORE_VERBOSE
        av_log(c->fctx, AV_LOG_MORE_VERBOSE, "index %d\n", nb_indices);
        av_log(c->fctx, AV_LOG_MORE_VERBOSE, "  frame_offset: %u (%d)\n", index->frame_offset, (int64_t)index->frame_offset << INDEX_OFFSET_SHIFT);
        av_log(c->fctx, AV_LOG_MORE_VERBOSE, "  frame_size: %u (%d)\n", index->frame_size, (int32_t)index->frame_size << INDEX_OFFSET_SHIFT);
        av_log(c->fctx, AV_LOG_MORE_VERBOSE, "  accum_audio_frame_count: %llu\n", index->accum_audio_frame_count);
        av_log(c->fctx, AV_LOG_MORE_VERBOSE, "  audio_frame_count: %u\n", index->audio_frame_count);
        av_log(c->fctx, AV_LOG_MORE_VERBOSE, "  encoding_q: %u\n", index->encoding_q);
#endif

        c->index_last = index;
        nb_indices++;

        // if (url_feof(pb)) { // XXX: url_feof doesn't work, why?
        if (avio_size(pb) < avio_tell(pb) + INDEX_ENTRY_SIZE) {
            index->next = NULL;
            return nb_indices;
        }

        index->next = av_malloc(sizeof(EarthsoftDVIndex));
        index = index->next;

        if (!index)
            return -1;
    }
}

#if 0
static int esdv_generate_index(EarthsoftDVDemuxContext *c, AVIOContext *pb)
{
    EarthsoftDVIndex *index;
    int nb_indices = 0;
    int i;
    uint64_t offset, pos;
    uint32_t video_size[4];

    offset = avio_tell(pb);

    if (avio_size(pb) < offset + FRAME_HEADER_FIXED_SIZE) {
        c->index_first = NULL;
        c->index_last = NULL;
        return 0;
    }

    c->index_first = av_malloc(sizeof(EarthsoftDVIndex));

    if (!c->index_first)
        return -1;

    index = c->index_first;

    for (;;) {
        index->frame = nb_indices;
        index->accum_audio_frame_count = avio_rb24(pb) << 24 | avio_rb24(pb);
        index->audio_frame_count       = avio_rb16(pb);

        avio_skip(pb, 244 + 8); // reserved + display aspect ratio

        index->encoding_q = avio_r8(pb);

        avio_skip(pb, 123); // reserved

        for (i = 0; i < 4; i++)
            video_size[i] = avio_rb32(pb);

        avio_skip(pb, 128 - 16); // reserved

        avio_skipto_align(pb, 4 * index->audio_frame_count, 4096);
        avio_skipto_align(pb, video_size[0], 32);

        if (c->video.interlaced) {
            avio_skipto_align(pb, video_size[1], 32);
            avio_skipto_align(pb, video_size[2], 32);
            avio_skipto_align(pb, video_size[3], 4096);
        }
        else {
            avio_skipto_align(pb, video_size[1], 4096);
        }

        pos = avio_tell(pb);

        index->frame_offset = offset >> INDEX_OFFSET_SHIFT;
        index->frame_size   = (pos - offset) >> INDEX_OFFSET_SHIFT;

#ifdef AV_LOG_MORE_VERBOSE
        av_log(c->fctx, AV_LOG_MORE_VERBOSE, "index %d\n", nb_indices);
        av_log(c->fctx, AV_LOG_MORE_VERBOSE, "  frame_offset: %u (%d)\n", index->frame_offset, (int64_t)index->frame_offset << INDEX_OFFSET_SHIFT);
        av_log(c->fctx, AV_LOG_MORE_VERBOSE, "  frame_size: %u (%d)\n", index->frame_size, (int32_t)index->frame_size << INDEX_OFFSET_SHIFT);
        av_log(c->fctx, AV_LOG_MORE_VERBOSE, "  accum_audio_frame_count: %llu\n", index->accum_audio_frame_count);
        av_log(c->fctx, AV_LOG_MORE_VERBOSE, "  audio_frame_count: %u\n", index->audio_frame_count);
        av_log(c->fctx, AV_LOG_MORE_VERBOSE, "  encoding_q: %u\n", index->encoding_q);
#endif

        c->index_last = index;
        offset = pos;
        nb_indices++;

        if (avio_size(pb) <= offset + FRAME_HEADER_FIXED_SIZE) {
            index->next = NULL;
            return nb_indices;
        }

        index->next = av_malloc(sizeof(EarthsoftDVIndex));
        index = index->next;

        if (!index)
            return -1;
    }
}
#endif

static void esdv_read_free(EarthsoftDVDemuxContext *c)
{
    if (c->has_audio)
        av_fifo_free(c->raw_audio_buffer);

    esdv_free_index(c->index_first);

    c->index_first    = NULL;
    c->index_last     = NULL;
    c->index_current  = NULL;
}

static int esdv_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    AVIOContext *f_index;
    EarthsoftDVDemuxContext *c = s->priv_data;
    char index_filename[1024];
    int flags, nb_frames;
    int i;

    s->ctx_flags |= AVFMTCTX_NOHEADER;

    c->vst = avformat_new_stream(s, NULL);

    if (!c->vst)
        return -1;

    c->fctx = s;

    /* read header of stream file */
    if (avio_r8(pb) != 'P' ||
        avio_r8(pb) != 'V' ||
        avio_r8(pb) != '3')
        return AVERROR_INVALIDDATA;

    c->video.codec_version = avio_r8(pb);

    if (c->video.codec_version != 2) {
        av_log(s, AV_LOG_ERROR, "codec version %d is not supported.\n", c->video.codec_version);
        return -1;
    }

    c->video.width  = avio_r8(pb) * 16;
    c->video.height = avio_r8(pb) * 8;

    flags = avio_r8(pb);

    c->video.interlaced = !(flags & 0x1);

    av_log(s, AV_LOG_DEBUG, "video format:\n");
    av_log(s, AV_LOG_DEBUG, "  codec version = %d\n", c->video.codec_version);
    av_log(s, AV_LOG_DEBUG, "  width         = %d\n", c->video.width);
    av_log(s, AV_LOG_DEBUG, "  height        = %d\n", c->video.height);
    av_log(s, AV_LOG_DEBUG, "  scanning      = %s\n", c->video.interlaced ? "interlaced" : "progressive");

    avio_skip(pb, 249); // reserved

    for (i = 0; i < 64; i++) {
        c->video.lum_quants[i] = avio_rb16(pb);
#ifdef AV_LOG_MORE_VERBOSE
        if (i == 0)
            av_log(s, AV_LOG_MORE_VERBOSE, "luminance quants:\n");
        av_log(s, AV_LOG_MORE_VERBOSE, "%3d, ", c->video.lum_quants[i]);
        if (i % 8 == 7)
            av_log(s, AV_LOG_MORE_VERBOSE, "\n");
#endif
    }

    for (i = 0; i < 64; i++) {
        c->video.chrom_quants[i] = avio_rb16(pb);
#ifdef AV_LOG_MORE_VERBOSE
        if (i == 0)
            av_log(s, AV_LOG_MORE_VERBOSE, "chrominance quants:\n");
        av_log(s, AV_LOG_MORE_VERBOSE, "%3d, ", c->video.chrom_quants[i]);
        if (i % 8 == 7)
            av_log(s, AV_LOG_MORE_VERBOSE, "\n");
#endif
    }

    avio_skip(pb, 15872); // reserved

    s->data_offset = avio_tell(pb);

    assert(FILE_HEADER_SIZE == s->data_offset);

    /* read (if exists) or generate index file */
    // XXX: in case extension isn't 'dv'
    snprintf(index_filename, sizeof(index_filename), "%si", s->filename); // *.dv -> *.dvi

    if (0 <= avio_open(&f_index, index_filename, AVIO_FLAG_READ)) {
        av_log(s, AV_LOG_INFO, "index file found: %s\n", index_filename);

        nb_frames = esdv_read_index(c, f_index);

        avio_close(f_index);
    }
    else {
        av_log(s, AV_LOG_INFO, "index file can't open or is missing\n");

        nb_frames = 0;

#if 0
        nb_frames = esdv_generate_index(c, pb);

        avio_seek(pb, s->data_offset, SEEK_SET);
#endif
    }

    if (nb_frames == 0) {
        av_log(s, AV_LOG_INFO, "contains no frames. propbably index and/or stream file is broken.\n");
    }
    else if (nb_frames < 0) {
        av_log(s, AV_LOG_ERROR, "allocation failed while reading index.\n");
        goto failed;
    }

    /* init video stream and codec params */
    c->vst->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    c->vst->codec->codec_id   = AV_CODEC_ID_EARTHSOFTDV;
    c->vst->codec->pix_fmt    = PIX_FMT_YUV422P;
    c->vst->codec->width      = c->video.width;
    c->vst->codec->height     = c->video.height;

    if (c->video.interlaced)
        avpriv_set_pts_info(c->vst, 64, earthsoftdv_framerate_interlaced.num, earthsoftdv_framerate_interlaced.den);
    else
        avpriv_set_pts_info(c->vst, 64, earthsoftdv_framerate_progressive.num, earthsoftdv_framerate_progressive.den);

    c->vst->start_time        = 0;
    c->vst->duration          = nb_frames;
    c->vst->nb_frames         = nb_frames;

    av_init_packet(&c->video_pkt);

    c->video_pkt.size         = sizeof(c->video);
    c->video_pkt.data         = (void*)&c->video;
    c->video_pkt.stream_index = c->vst->index;
    c->video_pkt.flags       |= AV_PKT_FLAG_KEY;

    if (!c->index_last || 0 < c->index_last->accum_audio_frame_count + c->index_last->audio_frame_count) {
        /* init audio stream and codec params */
        c->raw_audio_buffer = av_fifo_alloc(DEMUX_RAW_AUDIO_BUFFER_SIZE);

        if (!c->raw_audio_buffer) {
            av_log(c->fctx, AV_LOG_ERROR, "raw audio buffer allocation failed\n");
            goto failed;
        }

        c->ast        = NULL;
        c->has_audio  = 1;
    }
    else {
        /* no audio frame */
        c->ast        = NULL;
        c->has_audio  = 0;
    }

    /* initialize context */
    c->audio.accum_frame_count = 0;
    c->frame_current = 0;
    c->index_current = c->index_first;
    c->read_context = READ_FRAME_HEADER;

#ifdef DEMUX_STATISTICS
    c->vblock_size.max = 0;
    c->vblock_size.min = INT64_MAX;
    c->vblock_size.sum = 0;
    c->vblock_size.count = 0;
#endif

    return 0;

failed:
    esdv_read_free(c);

    return -1;
}

static int esdv_read_init_ast(EarthsoftDVDemuxContext *c, enum AVCodecID codec_id)
{
    c->ast = avformat_new_stream(c->fctx, NULL);

    if (!c->ast)
        return -1;

    c->ast->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    c->ast->codec->codec_id   = codec_id;
    c->ast->codec->sample_fmt = AV_SAMPLE_FMT_S16;
    c->ast->start_time        = 0;

    if (c->index_last)
        c->ast->nb_frames = c->index_last->accum_audio_frame_count + c->index_last->audio_frame_count;

    if (codec_id == AV_CODEC_ID_PCM_S16BE)
        c->ast->codec->channels = 2;

    return 0;
}

static int esdv_read_frame_header(EarthsoftDVDemuxContext *c, AVIOContext *pb)
{
    int i;

    if (c->index_current) {
        assert(avio_tell(pb) == ((int64_t)c->index_current->frame_offset << INDEX_OFFSET_SHIFT));
        assert(avio_tell(pb) + FRAME_HEADER_FIXED_SIZE < avio_size(pb));
    }

    av_log(c->fctx, AV_LOG_DEBUG, "reading frame %d header\n", c->frame_current);

    c->audio.block.accum_frame_count = avio_rb24(pb) << 24 | avio_rb24(pb); // 48bit: sum of audio frame count to this frame
    c->audio.block.frame_count = avio_rb16(pb); // 16bit: audio frame count in this frame
    c->audio.block.sample_rate = avio_rb32(pb); // 32bit: audio sample rate in Hz

#ifdef AV_LOG_MORE_VERBOSE
    av_log(c->fctx, AV_LOG_MORE_VERBOSE, "  audio frame count = %u\n", c->audio.block.frame_count);
    av_log(c->fctx, AV_LOG_MORE_VERBOSE, "  audio sample rate = %u Hz\n", c->audio.block.sample_rate);
#endif

    avio_skip(pb, 244); // reserved

    c->video.block.dar = (AVRational){avio_rb16(pb), avio_rb16(pb)}; // 16bit*2: display aspect ratio
    c->video.block.encoding_q = avio_r8(pb); // 8bit: encoding quality

    avio_skip(pb, 123); // reserved

    for (i = 0; i < 4; i++) { // 32bit * 4: size of video block
        c->video.block.size[i] = avio_rb32(pb);
#ifdef AV_LOG_MORE_VERBOSE
        av_log(c->fctx, AV_LOG_MORE_VERBOSE, "  size video%d = %u\n", i, c->video.block.size[i]);
#endif
    }

    avio_skip(pb, 112); // reserved

    return 0;
}

#define FORCE_PCM_LIMIT (1536 * 4) // XXX
static int esdv_demux_audio_pkt(EarthsoftDVDemuxContext *c, AVPacket *pkt)
{
    int buf_size = c->audio.block.frame_count * 4;
    int fifo_size;
    int ret;
    int nonpcm_packet_ofs = 0;
    enum AVCodecID codec_id = AV_CODEC_ID_NONE;

    /* determines audio codec */
    {
        uint8_t *buf = c->audio.block.buffer;
        int buf_ofs;

        for (buf_ofs = 0; buf_ofs < buf_size;) {
            /* search for IEC 61937 Burst-preamble */
            if (buf[buf_ofs++] == 0xf8 && buf[buf_ofs++] == 0x72 &&
                 buf[buf_ofs++] == 0x4e && buf[buf_ofs++] == 0x1f) { // sync word (Pa = F872h, Pb = 4E1Fh)
                uint16_t pc = ((uint16_t)buf[buf_ofs] << 8) | buf[buf_ofs + 1];
                //uint16_t pd = ((uint16_t)buf[buf_ofs + 2] << 8) | buf[buf_ofs + 3];

                buf_ofs += 4;

                /* Data-type */
                switch (pc & 0x1f) {
                // XXX: ignore null and pause
                case 0x00: /* Null data */
                case 0x03: /* Pause */

                // unsupported data types
                case 0x02: /* SMPTE 338M */
                case 0x04: /* MPEG-1 layer 1 data */
                case 0x05: /* MPEG-1 layer 2 or 3 data or MPEG-2 without extension */
                case 0x06: /* MPEG-2 data with extension */
                case 0x07: /* MPEG-2 AAC */
                case 0x08: /* MPEG-2, layer-1 low sampling frequency */
                case 0x09: /* MPEG-2, layer-2 low sampling frequency */
                case 0x0a: /* MPEG-2, layer-3 low sampling frequency */
                case 0x0b: /* DTS type I */
                case 0x0c: /* DTS type II */
                case 0x0d: /* DTS type III */
                case 0x0e: /* ATRAC */
                case 0x0f: /* ATRAC 2/3 */
                case 0x10: /* ATRAC-X */
                case 0x11: /* DTS type IV */
#if !CONFIG_WMAPRO_DECODER
                case 0x12: /* WMA professional */
#endif
                case 0x13: /* MPEG-2 AAC low sampling frequency */
                case 0x16: /* MAT */
                case 0x17: case 0x18: case 0x19: case 0x1a: /* Reserved */
                case 0x1b: case 0x1c: case 0x1d: case 0x1e: /* SMPTE 338M */
                case 0x1f: /* Extended data-type */
                    av_log(c->fctx, AV_LOG_ERROR, "  unsupported IEC 61937 data type %d\n", pc & 0x1f);
                    return -1;

                case 0x01: /* AC-3 data */
                    av_log(c->fctx, AV_LOG_DEBUG, "  AC-3 data stream detedted\n");
                    c->nonpcm_packet_size = 1536 * 4;
                    codec_id = AV_CODEC_ID_AC3;
                    break;

#if CONFIG_WMAPRO_DECODER
                case 0x12: /* WMA professional */
                    switch ((pc >> 5) & 0x3) {
                    case 0: // type I
                        av_log(c->fctx, AV_LOG_DEBUG, "  WMA professional type I data stream detedted\n");
                        c->nonpcm_packet_size = 2048 * 4;
                        break;
                    case 1: // type II
                        av_log(c->fctx, AV_LOG_DEBUG, "  WMA professional type II data stream detedted\n");
                        c->nonpcm_packet_size = 2048 * 4;
                        break;
                    case 2: // type III
                        av_log(c->fctx, AV_LOG_DEBUG, "  WMA professional type III data stream detedted\n");
                        c->nonpcm_packet_size = 1024 * 4;
                        break;
                    case 3: // type IV
                        av_log(c->fctx, AV_LOG_DEBUG, "  WMA professional type IV data stream detedted\n");
                        c->nonpcm_packet_size = 512 * 4;
                        break;
                    default:
                        return -1;
                    }

                    codec_id = AV_CODEC_ID_WMAPRO;
                    break;
#endif

                case 0x14: /* MPEG-4 AAC */
                    av_log(c->fctx, AV_LOG_DEBUG, "  MPEG-4 AAC data stream detedted\n");

                    switch ((pc >> 5) & 0x3) {
                    case 0:
                        c->nonpcm_packet_size = 1024 * 4;
                        break;
                    case 1:
                        c->nonpcm_packet_size = 2048 * 4;
                        break;
                    case 2:
                        c->nonpcm_packet_size = 4096 * 4;
                        break;
                    case 3:
                        c->nonpcm_packet_size = 512 * 4;
                        break;
                    default:
                        return -1;
                    }

                    codec_id = AV_CODEC_ID_AAC;
                    break;

                case 0x15: /* Enhanced AC-3 */
                    av_log(c->fctx, AV_LOG_DEBUG, "  Enhanced AC-3 data stream detedted\n");
                    c->nonpcm_packet_size = 6144 * 4;
                    codec_id = AV_CODEC_ID_EAC3;
                    break;
                }

                if (!c->ast) {
                    ret = esdv_read_init_ast(c, codec_id);

                    if (ret < 0)
                        return -1;

                    /* discards unsynced buffer */
                    av_fifo_drain(c->raw_audio_buffer, av_fifo_size(c->raw_audio_buffer));
                }

                c->nonpcm_packet_pts  = c->audio.accum_frame_count + buf_ofs / 4;
                nonpcm_packet_ofs     = buf_ofs;

                break;
            }
        }
    }

    fifo_size = av_fifo_size(c->raw_audio_buffer);

    if (DEMUX_RAW_AUDIO_BUFFER_SIZE <= fifo_size + buf_size) {
        av_log(c->fctx, AV_LOG_ERROR, "raw audio buffer overflow\n");
        return -1;
    }

    /* generate packet */
    if (!c->ast) {
        if (FORCE_PCM_LIMIT <= fifo_size) {
            /* IEC 61937 Burst-preamble not found, determine as linear PCM */
            ret = esdv_read_init_ast(c, AV_CODEC_ID_PCM_S16BE);

            if (ret < 0)
                return -1;

            ret = av_new_packet(pkt, fifo_size + buf_size);

            if (ret < 0)
                return ret;

            av_fifo_generic_read(c->raw_audio_buffer, pkt->data, fifo_size, NULL);

            memcpy(pkt->data + fifo_size, c->audio.block.buffer, buf_size);

            pkt->duration     = pkt->size / 4;
            pkt->pts          = AV_NOPTS_VALUE;
            pkt->dts          = AV_NOPTS_VALUE;

            return pkt->size;
        }
        else {
            av_fifo_generic_write(c->raw_audio_buffer, c->audio.block.buffer, buf_size, NULL);

            /* return empty packet */
            return 0;
        }
    }
    else if (c->ast->codec->codec_id == AV_CODEC_ID_PCM_S16BE) {
        /* linear PCM */
        ret = av_new_packet(pkt, buf_size);

        if (ret < 0)
            return ret;

        memcpy(pkt->data, c->audio.block.buffer, buf_size);

        pkt->duration     = pkt->size / 4;
        pkt->pts          = c->audio.accum_frame_count;
        pkt->dts          = AV_NOPTS_VALUE;

        return pkt->size;
    }
    else {
        /* non-linear PCM */
        if (fifo_size + buf_size < c->nonpcm_packet_size) {
            if (0 < fifo_size)
                av_fifo_generic_write(c->raw_audio_buffer, c->audio.block.buffer, buf_size, NULL);
            else
                av_fifo_generic_write(c->raw_audio_buffer, c->audio.block.buffer + nonpcm_packet_ofs, buf_size - nonpcm_packet_ofs, NULL);

            /* return empty packet */
            return 0;
        }

        ret = av_new_packet(pkt, c->nonpcm_packet_size);

        if (ret < 0)
            return ret;

        av_fifo_generic_read(c->raw_audio_buffer, pkt->data, fifo_size, NULL);

        memcpy(pkt->data + fifo_size, c->audio.block.buffer, c->nonpcm_packet_size - fifo_size);

        pkt->duration = 0; // XXX
        pkt->pts      = c->nonpcm_packet_pts;
        pkt->dts      = AV_NOPTS_VALUE;

        av_fifo_generic_write(c->raw_audio_buffer, c->audio.block.buffer + nonpcm_packet_ofs, buf_size - nonpcm_packet_ofs, NULL);

        return pkt->size;
    }
}

static int esdv_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *pb = s->pb;
    EarthsoftDVDemuxContext *c = s->priv_data;
    int i, ret;

    if (c->read_context == READ_FRAME_HEADER) {
        esdv_read_frame_header(c, pb);

        if (pb->eof_reached) {
            av_log(s, AV_LOG_DEBUG, "end of stream reached\n");
            return AVERROR_EOF;
        }

        c->read_context = READ_AUDIO_BLOCK;
    }

    if (c->read_context == READ_AUDIO_BLOCK) {
        /* read and generate audio packet */
        av_log(s, AV_LOG_DEBUG, "reading frame %d audio block\n", c->frame_current);

        c->read_context = READ_VIDEO_BLOCK;

        if (c->has_audio) {
            if (EARTHSOFTDV_MAX_AUDIO_SAMPLE_RATE < c->audio.block.sample_rate) {
                av_log(c->fctx, AV_LOG_ERROR, "audio sample rate is %u Hz, but currently supported up to %u Hz\n", c->audio.block.sample_rate, EARTHSOFTDV_MAX_AUDIO_SAMPLE_RATE);
                return -1;
            }

            avio_read(pb, c->audio.block.buffer, c->audio.block.frame_count * 4);

            avio_seekto_align(pb, 4096);

            // XXX: in case audio format changed
            ret = esdv_demux_audio_pkt(c, pkt);

            c->audio.accum_frame_count += c->audio.block.frame_count;

            if (ret < 0) {
                return ret;
            }
            else if (ret) {
                // XXX: in case sample rate changed
                avpriv_set_pts_info(c->ast, 64, 1, c->audio.block.sample_rate);

                c->ast->codec->sample_rate = c->audio.block.sample_rate;

                if (c->ast->codec->codec_id == AV_CODEC_ID_PCM_S16BE)
                    c->ast->codec->bit_rate = 2 * c->audio.block.sample_rate * 16;

                pkt->stream_index = c->ast->index;
                pkt->flags       |= AV_PKT_FLAG_KEY;

                return pkt->size;
            }
        }
        else {
            avio_seekto_align(pb, 4096);
        }
    }

    if (c->read_context == READ_VIDEO_BLOCK) {
        /* read and generate video packet */
        av_log(s, AV_LOG_DEBUG, "reading frame %d video block\n", c->frame_current);

        for (i = 0; i < (c->video.interlaced ? 4 : 2); i++) {
            if (EARTHSOFTDV_MAX_VIDEO_BLOCK_SIZE < c->video.block.size[i]) {
                av_log(s, AV_LOG_ERROR, "buffer overflow at frame %d, video block %d (size=%u)\n", c->frame_current, i, c->video.block.size[i]);
                return -1;
            }
#ifdef DEMUX_STATISTICS
            c->vblock_size.max = FFMAX(c->vblock_size.max, c->video.block.size[i]);
            c->vblock_size.min = FFMIN(c->vblock_size.min, c->video.block.size[i]);
#endif
        }

        avio_read(pb, c->video.block.buffer[0], c->video.block.size[0]);
        avio_seekto_align(pb, 32);

        avio_read(pb, c->video.block.buffer[1], c->video.block.size[1]);

        if (c->video.interlaced) {
            avio_seekto_align(pb, 32);
            avio_read(pb, c->video.block.buffer[2], c->video.block.size[2]);

            avio_seekto_align(pb, 32);
            avio_read(pb, c->video.block.buffer[3], c->video.block.size[3]);
        }

        avio_seekto_align(pb, 4096);

        /* set display aspect ratio */
        av_reduce(&c->vst->sample_aspect_ratio.num,
                  &c->vst->sample_aspect_ratio.den,
                  c->video.width * c->video.height / c->video.block.dar.den, // XXX
                  c->video.width * c->video.width / c->video.block.dar.num, // XXX
                  1024 * 1024);

        c->video_pkt.duration = 1;
        c->video_pkt.pts      = c->frame_current;
        c->video_pkt.dts      = AV_NOPTS_VALUE;

        c->read_context = READ_FRAME_HEADER;
        c->frame_current++;

        if (c->index_current)
            c->index_current = c->index_current->next;

        *pkt = c->video_pkt;

        return pkt->size;
    }

    return -1;
}

static int esdv_read_seek(AVFormatContext *s, int stream_index,
                            int64_t timestamp, int flags)
{
    EarthsoftDVDemuxContext *c = s->priv_data;
    int64_t i, offset;
    EarthsoftDVIndex *index_seek;

    if (!c->index_first)
        return -1;

    index_seek = c->index_first;

    for (i = 0; i < timestamp; i++) {
        index_seek = index_seek->next;
        if (!index_seek)
            return -1;
    }

    c->read_context = READ_FRAME_HEADER;
    c->frame_current = timestamp;
    c->index_current = index_seek;

    offset = avio_seek(s->pb, (int64_t)c->index_current->frame_offset << INDEX_OFFSET_SHIFT, SEEK_SET);

    return (offset < 0) ? offset : 0;
}

static int esdv_read_close(AVFormatContext *s)
{
    EarthsoftDVDemuxContext *c = s->priv_data;

    esdv_read_free(c);

#ifdef DEMUX_STATISTICS
    av_log(s, AV_LOG_INFO, "video block size: max %lld (%lld) min %lld (%lld)\n",
         c->vblock_size.max,
         c->vblock_size.max >> INDEX_OFFSET_SHIFT,
         c->vblock_size.min,
         c->vblock_size.min >> INDEX_OFFSET_SHIFT);
#endif

    return 0;
}

#endif // if CONFIG_EARTHSOFTDV_DEMUXER

#if CONFIG_EARTHSOFTDV_MUXER

#define MAX_BLOCK_BUFFER        3
#define MUX_RAW_AUDIO_BUFFER_SIZE (EARTHSOFTDV_MAX_AUDIO_BLOCK_SIZE * MAX_BLOCK_BUFFER)
#define VIDEO_BLOCK_BUFFER_SIZE   (sizeof(EarthsoftDVVideoBlock) * MAX_BLOCK_BUFFER)
#define AUDIO_BLOCK_BUFFER_SIZE   (sizeof(EarthsoftDVAudioBlock) * MAX_BLOCK_BUFFER)

#define WRITE_FILE_HEADER   0
#define WRITE_FRAME         1

typedef struct EarthsoftDVMuxContext {
    AVFormatContext *fctx;
    AVIOContext *f_index;
    int write_context;
    int nb_frames;
    int nb_frames_audio;
    int nb_audio_frames_per_block;
    int contain_audio;
    AVFifoBuffer *raw_audio_buffer;
    AVFifoBuffer *video_block_buffer;
    AVFifoBuffer *audio_block_buffer;
    EarthsoftDVVideoContext video;
    EarthsoftDVAudioContext audio;
} EarthsoftDVMuxContext;

static void esdv_write_free(EarthsoftDVMuxContext *c)
{
    av_fifo_free(c->raw_audio_buffer);
    av_fifo_free(c->video_block_buffer);
    av_fifo_free(c->audio_block_buffer);

    avio_close(c->f_index);
}

static int esdv_write_header(AVFormatContext *s)
{
    EarthsoftDVMuxContext *c = s->priv_data;
    AVCodecContext *enc;
    AVStream *vst = NULL;
    AVStream *ast = NULL;
    char index_filename[1024];
    int i;

    c->fctx = s;

    /* get and verify supplied stream */
    for (i = 0; i < s->nb_streams; i++) {
        enc = s->streams[i]->codec;

        switch(enc->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            if (enc->codec_id == AV_CODEC_ID_EARTHSOFTDV) {
                if (vst) {
                    av_log(c->fctx, AV_LOG_ERROR, "too many video streams supplied. the file can contain exactly one video stream.\n");
                    return -1;
                }
                vst = s->streams[i];
            }
            break;
        case AVMEDIA_TYPE_AUDIO:
            // TODO: supporting non PCM audio (AAC, AC3 etc.)
            if (enc->codec_id == AV_CODEC_ID_PCM_S16BE && enc->channels == 2) {
                if (ast) {
                    av_log(c->fctx, AV_LOG_ERROR, "too many audio streams supplied. the file can contain exactly one audio stream.\n");
                    return -1;
                }
                ast = s->streams[i];
            }
            break;
        }
    }

    if (!vst) {
        av_log(s, AV_LOG_ERROR, "Earthsoft DV stream must be supplied");
        goto failed;
    }

    if (ast) {
        if (EARTHSOFTDV_MAX_AUDIO_SAMPLE_RATE < ast->codec->sample_rate) {
            av_log(c->fctx, AV_LOG_ERROR, "sample rate of supplied stream is %d Hz, but currently supported up to %d Hz\n", ast->codec->sample_rate, EARTHSOFTDV_MAX_AUDIO_SAMPLE_RATE);
            goto failed;
        }

        c->contain_audio = 1;
    }
    else {
        // TODO: supporting non PCM audio (AAC, AC3 etc.)
        av_log(s, AV_LOG_INFO, "stereo pcm_s16be stream is not supplied, output stream will contain no audio\n");

        c->contain_audio = 0;
    }

    /* alloc buffers */
    c->video_block_buffer = av_fifo_alloc(VIDEO_BLOCK_BUFFER_SIZE);

    if (!c->video_block_buffer) {
        av_log(c->fctx, AV_LOG_ERROR, "video block buffer allocation failed\n");
        goto failed;
    }

    if (c->contain_audio) {
        // XXX: in case sample rate changed
        c->audio.sample_rate = ast->codec->sample_rate;

        c->raw_audio_buffer = av_fifo_alloc(MUX_RAW_AUDIO_BUFFER_SIZE);

        if (!c->raw_audio_buffer) {
            av_log(c->fctx, AV_LOG_ERROR, "raw audio buffer allocation failed\n");
            goto failed;
        }

        c->audio_block_buffer = av_fifo_alloc(AUDIO_BLOCK_BUFFER_SIZE);

        if (!c->audio_block_buffer) {
            av_log(c->fctx, AV_LOG_ERROR, "audio block buffer allocation failed\n");
            goto failed;
        }
    }

    /* open index file */
    // XXX: in case extension isn't 'dv'
    snprintf(index_filename, sizeof(index_filename), "%si", s->filename); // *.dv -> *.dvi

    if (avio_open(&c->f_index, index_filename, AVIO_FLAG_WRITE) < 0) {
        av_log(s, AV_LOG_ERROR, "can't open index file\n");
        goto failed;
    }
    else {
        av_log(s, AV_LOG_INFO, "output index file to '%s'\n", index_filename);
    }

    /* initialize context */
    c->nb_frames = 0;
    c->nb_frames_audio = 0;
    c->nb_audio_frames_per_block = 0;
    c->audio.accum_frame_count = 0;
    c->write_context = WRITE_FILE_HEADER;

    return 0;

failed:
    esdv_write_free(c);

    return -1;
}

static inline void esdv_calc_nb_audio_frame(EarthsoftDVMuxContext *c)
{
    if (c->video.interlaced)
        c->nb_audio_frames_per_block = (((uint64_t)c->nb_frames_audio + 1)
                                          * c->audio.sample_rate
                                          * earthsoftdv_framerate_interlaced.num
                                       ) / earthsoftdv_framerate_interlaced.den
                                         - c->audio.accum_frame_count;
    else
        c->nb_audio_frames_per_block = (((uint64_t)c->nb_frames_audio + 1)
                                          * c->audio.sample_rate
                                          * earthsoftdv_framerate_progressive.num
                                       ) / earthsoftdv_framerate_progressive.den
                                         - c->audio.accum_frame_count;
}

static void esdv_flush_header(AVIOContext *pb, EarthsoftDVMuxContext *c)
{
    uint8_t flags = (c->video.interlaced ? 0x0 : 0x1);
    int i;

    av_log(c->fctx, AV_LOG_DEBUG, "writing header\n");

    avio_w8(pb, 'P');
    avio_w8(pb, 'V');
    avio_w8(pb, '3');

    avio_w8(pb, c->video.codec_version);
    avio_w8(pb, c->video.width / 16);
    avio_w8(pb, c->video.height / 8);
    avio_w8(pb, flags);

    avio_skip(pb, 249); // reserved

    for (i = 0; i < 64; i++) {
        avio_wb16(pb, c->video.lum_quants[i]);
    }

    for (i = 0; i < 64; i++) {
        avio_wb16(pb, c->video.chrom_quants[i]);
    }

    avio_seek(pb, FILE_HEADER_SIZE, SEEK_SET); // reserved

    esdv_calc_nb_audio_frame(c);
}

static int esdv_flush_frame(AVIOContext *pb, EarthsoftDVMuxContext *c)
{
    int i;
    int64_t offset = avio_tell(pb);

    /* frame header */
    av_log(c->fctx, AV_LOG_DEBUG, "  writing frame header\n");

    avio_wb24(pb, c->audio.block.accum_frame_count >> 24);
    avio_wb24(pb, c->audio.block.accum_frame_count & 0x000000ffffff);
    avio_wb16(pb, c->audio.block.frame_count);
    avio_wb32(pb, c->audio.block.sample_rate);

    avio_skip(pb, 244); // reserved

    avio_wb16(pb, c->video.block.dar.num);
    avio_wb16(pb, c->video.block.dar.den);
    avio_w8(pb, c->video.block.encoding_q);

    avio_skip(pb, 123); // reserved

    if (c->video.interlaced) {
        for (i = 0; i < 4; i++) {
            avio_wb32(pb, c->video.block.size[i]);
        }
    }
    else {
        for (i = 0; i < 2; i++) {
            avio_wb32(pb, c->video.block.size[i]);
        }
        for (i = 2; i < 4; i++) {
            avio_wb32(pb, 0);
        }
    }

    avio_skip(pb, 112); // reserved

    /* audio block */
    av_log(c->fctx, AV_LOG_DEBUG, "  writing audio block\n");

    avio_write(pb, c->audio.block.buffer, 4 * c->audio.block.frame_count);

    avio_seekto_align(pb, 4096);

    /* video block */
    av_log(c->fctx, AV_LOG_DEBUG, "  writing video block\n");
#ifdef AV_LOG_MORE_VERBOSE
    for (i = 0; i < 4; i++) {
        av_log(c->fctx, AV_LOG_MORE_VERBOSE, "  size video%d = %u\n", i, c->video.block.size[i]);
    }
#endif

    avio_write(pb, c->video.block.buffer[0], c->video.block.size[0]);
    avio_seekto_align(pb, 32);

    avio_write(pb, c->video.block.buffer[1], c->video.block.size[1]);

    if (c->video.interlaced) {
        avio_seekto_align(pb, 32);
        avio_write(pb, c->video.block.buffer[2], c->video.block.size[2]);
        avio_seekto_align(pb, 32);
        avio_write(pb, c->video.block.buffer[3], c->video.block.size[3]);
    }

    avio_seekto_align(pb, 4096);

    avio_flush(pb);

    /* index */
    avio_wb32(c->f_index, offset >> INDEX_OFFSET_SHIFT);
    avio_wb16(c->f_index, (avio_tell(pb) - offset) >> INDEX_OFFSET_SHIFT);
    avio_wb24(c->f_index, c->audio.accum_frame_count >> 24);
    avio_wb24(c->f_index, c->audio.accum_frame_count & 0x000000ffffff);
    avio_wb16(c->f_index, c->audio.block.frame_count);
    avio_w8(c->f_index, c->video.block.encoding_q);
    avio_w8(c->f_index, 0); // reserved

    avio_flush(c->f_index);

    return 0;
}

static int esdv_flush_block_buffer(AVIOContext *pb, EarthsoftDVMuxContext *c)
{
    while (sizeof(c->video.block) <= av_fifo_size(c->video_block_buffer) &&
          (!c->contain_audio || sizeof(c->audio.block) <= av_fifo_size(c->audio_block_buffer))) {
        /* if one or more audio and video blocks were enqueued, then write them to the file */
        av_fifo_generic_read(c->video_block_buffer, &c->video.block, sizeof(c->video.block), NULL);

        if (c->contain_audio) {
            av_fifo_generic_read(c->audio_block_buffer, &c->audio.block, sizeof(c->audio.block), NULL);
        }
        else {
            memset(&c->audio.block, 0, sizeof(c->audio.block));

            /*
             * sets dummy sample rate
             *   Earthsoft's DV player can't play stream that contains
             *   no audio frames (frame count == 0) and sample rate == 0.
             *   but if sample rate != 0, player can play such stream.
             */
            c->audio.block.sample_rate = 48000;
        }

        av_log(c->fctx, AV_LOG_DEBUG, "writing frame %d\n", c->nb_frames);

        esdv_flush_frame(pb, c);

        c->nb_frames++;
    }

    return 0;
}

static int esdv_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *pb = s->pb;
    EarthsoftDVMuxContext *c = s->priv_data;
    EarthsoftDVVideoContext *video;
    AVStream* st = s->streams[pkt->stream_index];
    int i, ret;

    if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO /* && c->contain_audio */) {
        if (c->audio.sample_rate != st->codec->sample_rate) {
            av_log(s, AV_LOG_ERROR, "audio sample rate changed, unsupported feature\n");
            return -1;
        }

        /* enqueueing audio packet into queue */
        // TODO: supporting non PCM audio (AAC, AC3 etc.)
#ifdef AV_LOG_MORE_VERBOSE
        av_log(s, AV_LOG_MORE_VERBOSE, "enqueueing audio packet (%d frames)\n", pkt->size / 4);
#endif

        if (MUX_RAW_AUDIO_BUFFER_SIZE <= av_fifo_size(c->raw_audio_buffer) + pkt->size) {
            av_log(s, AV_LOG_ERROR, "raw audio buffer overflow\n");
            return -1;
        }

        av_fifo_generic_write(c->raw_audio_buffer, pkt->data, pkt->size, NULL);

        if (c->write_context == WRITE_FILE_HEADER)
            return 0; // because c->nb_audio_frames_per_block is not calculated

        while (c->nb_audio_frames_per_block * 4 <= av_fifo_size(c->raw_audio_buffer)) {
            c->audio.block.frame_count = c->nb_audio_frames_per_block;
            c->audio.block.sample_rate = c->audio.sample_rate;
            c->audio.block.accum_frame_count = c->audio.accum_frame_count;

            av_fifo_generic_read(c->raw_audio_buffer, c->audio.block.buffer, c->nb_audio_frames_per_block * 4, NULL);

            /* enqueueing audio blocks into queue */
#ifdef AV_LOG_MORE_VERBOSE
            av_log(s, AV_LOG_DEBUG, "enqueueing audio block (%d frames, current size = %d)\n",
                                    c->nb_audio_frames_per_block, av_fifo_size(c->audio_block_buffer) / sizeof(c->audio.block));
#endif

            if (AUDIO_BLOCK_BUFFER_SIZE <= av_fifo_size(c->audio_block_buffer)) {
                av_log(s, AV_LOG_ERROR, "audio block buffer overflow\n");
                return -1;
            }

            av_fifo_generic_write(c->audio_block_buffer, &c->audio.block, sizeof(c->audio.block), NULL);

            c->audio.accum_frame_count += c->nb_audio_frames_per_block;
            c->nb_frames_audio++;

            esdv_calc_nb_audio_frame(c);
        }
    }
    else if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
        video = (EarthsoftDVVideoContext*)pkt->data;

        if (c->write_context == WRITE_FILE_HEADER) {
            /* write file header first */
            // XXX: use memcpy
            c->video.codec_version  = video->codec_version;
            c->video.interlaced     = video->interlaced;
            c->video.width          = video->width;
            c->video.height         = video->height;

            for (i = 0; i < 64; i++) {
                c->video.lum_quants[i]   = video->lum_quants[i];
                c->video.chrom_quants[i] = video->chrom_quants[i];
            }

            esdv_flush_header(pb, c);

            c->write_context = WRITE_FRAME;
        }

        /* enqueueing video blocks into queue */
#ifdef AV_LOG_MORE_VERBOSE
        av_log(s, AV_LOG_MORE_VERBOSE, "enqueueing video blocks (current size = %d)\n",
                                av_fifo_size(c->video_block_buffer) / sizeof(c->video.block));
#endif

        if (VIDEO_BLOCK_BUFFER_SIZE <= av_fifo_size(c->video_block_buffer)) {
            av_log(s, AV_LOG_ERROR, "video block buffer overflow\n");
            return -1;
        }

        av_fifo_generic_write(c->video_block_buffer, &video->block, sizeof(video->block), NULL);
    }

    if (c->write_context == WRITE_FRAME) {
        ret = esdv_flush_block_buffer(pb, c);

        if (ret < 0)
            return -1;
    }

    return 0;
}

static int esdv_write_trailer(AVFormatContext *s)
{
    EarthsoftDVMuxContext *c = s->priv_data;
    AVIOContext *pb = s->pb;
    int ret;

    if (c->contain_audio) {
        /* fill audio blocks until all video blocks is flushed */
        if (0 < av_fifo_size(c->raw_audio_buffer)) {
            c->audio.block.frame_count = av_fifo_size(c->raw_audio_buffer) / 4;

            av_fifo_generic_read(c->raw_audio_buffer, c->audio.block.buffer, c->audio.block.frame_count * 4, NULL);
        }

        while (sizeof(c->video.block) <= av_fifo_size(c->video_block_buffer)) {
            memset(&c->audio.block.buffer[c->audio.block.frame_count * 4], 0,
                     (c->nb_audio_frames_per_block - c->audio.block.frame_count) * 4);

            c->audio.block.frame_count = c->nb_audio_frames_per_block;
            c->audio.block.sample_rate = c->audio.sample_rate;
            c->audio.block.accum_frame_count = c->audio.accum_frame_count;

            av_fifo_generic_write(c->audio_block_buffer, &c->audio.block, sizeof(c->audio.block), NULL);

            ret = esdv_flush_block_buffer(pb, c);

            if (ret < 0)
                return ret;

            c->nb_frames += ret;

            c->audio.block.frame_count = 0;
            c->audio.accum_frame_count += c->nb_audio_frames_per_block;
            c->nb_frames_audio++;

            esdv_calc_nb_audio_frame(c);
        }
    }
    else {
        ret = esdv_flush_block_buffer(pb, c);

        if (ret < 0)
            return ret;
    }

    avio_flush(pb);
    avio_flush(c->f_index);

    esdv_write_free(c);

    return 0;
}

#endif // if CONFIG_EARTHSOFTDV_MUXER

#if CONFIG_EARTHSOFTDV_MUXER
AVOutputFormat ff_earthsoftdv_muxer = {
    .name           = "earthsoftdv",
    .long_name      = NULL_IF_CONFIG_SMALL("Earthsoft PV3/PV4 DV video format"),
    .extensions     = "dv", // XXX: conflicts with extension of 'DV'
    .priv_data_size = sizeof(EarthsoftDVMuxContext),
    .audio_codec    = AV_CODEC_ID_PCM_S16BE, // TODO: supporting non PCM audio (AAC, AC3 etc.)
    .video_codec    = AV_CODEC_ID_EARTHSOFTDV,
    .write_header   = esdv_write_header,
    .write_packet   = esdv_write_packet,
    .write_trailer  = esdv_write_trailer,
};
#endif /* if CONFIG_EARTHSOFTDV_MUXER */

#if CONFIG_EARTHSOFTDV_DEMUXER
AVInputFormat ff_earthsoftdv_demuxer = {
    .name           = "earthsoftdv",
    .long_name      = NULL_IF_CONFIG_SMALL("Earthsoft PV3/PV4 DV video format"),
    .extensions     = "dv", // XXX: conflicts with extension of 'DV'
    .priv_data_size = sizeof(EarthsoftDVDemuxContext),
    .read_probe     = esdv_read_probe,
    .read_header    = esdv_read_header,
    .read_packet    = esdv_read_packet,
    .read_close     = esdv_read_close,
    .read_seek      = esdv_read_seek,
};
#endif /* if CONFIG_EARTHSOFTDV_DEMUXER */
