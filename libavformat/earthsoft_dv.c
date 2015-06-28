/*
 * Earthsoft DV Demuxer.
 * Copyright (c) 2015 Jan Ekström
 *
 * This file is part of Libav.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "avformat.h"
#include "internal.h"

typedef struct EarthsoftDVDemuxContext {
    int codec_version;
    int64_t width;
    int64_t height;
    int progressive_scan;
    uint16_t luma_quantizers[64];
    uint16_t chroma_quantizers[64];
    int64_t data_offset;
} EarthsoftDVDemuxContext;

static int earthsoft_probe(AVProbeData *p) {
    if (p->buf[0] == 'P' && p->buf[1] == 'V' && p->buf[2] == '3')
        return AVPROBE_SCORE_MAX * 3 / 4;
    else
        return 0;
}

static int create_video_stream(AVFormatContext *s) {
    EarthsoftDVDemuxContext *c  = s->priv_data;
    AVStream *st = NULL;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id   = AV_CODEC_ID_EARTHSOFT_DV;
    st->codec->width      = c->width;
    st->codec->height     = c->height;
    avpriv_set_pts_info(st, 64, 1001, c->progressive_scan ? 30000 : 60000);

    st->codec->extradata_size = sizeof(c->luma_quantizers) +
                                sizeof(c->chroma_quantizers);

    st->codec->extradata = av_malloc(st->codec->extradata_size +
                                     FF_INPUT_BUFFER_PADDING_SIZE);
    if (!st->codec->extradata) {
        av_log(s, AV_LOG_ERROR, "Failed to allocate extradata\n");
        return AVERROR(ENOMEM);
    }

    /* copy the quantizers to the codec extradata */
    memcpy(c->luma_quantizers, st->codec->extradata,
           sizeof(c->luma_quantizers));
    memcpy(c->chroma_quantizers,
           st->codec->extradata + sizeof(c->luma_quantizers),
           sizeof(c->chroma_quantizers));

    return 0;
}

static int create_audio_stream(AVFormatContext *s) {
    AVStream *st = NULL;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id   = AV_CODEC_ID_PCM_S16BE;
    st->codec->channels   = 2;

    return 0;
}

/*
    Earthsoft DV File (.dv)

    One header, followed by an arbitrary amount of A/V frames.

    8 bits per byte. Multiple byte data is big endian.

    Header:

      offset (bytes)    length (bytes)
    -       0               1   'P'
    -       1               1   'V'
    -       2               1   '3'
    -       3               1   Codec Version
    -       4               1   Horizontal resolution / 16
    -       5               1   Vertical resolution / 8
    -       6               1   Encoding type (0: interlaced, 1: progressive)
    -       7             249   Reserved
    -     256             128   Luminance Quantization Table
    -     384             128   Chroma Quantization Table
    -     512           15872   Reserved
    -   16384               -   Beginning of the first A/V frame

    A/V Frame:

      offset (bytes)    length (bytes)
    -       0               6   Amount of audio frames until the earlier A/V Frame
    -       6               2   Amount of audio frames in this A/V Frame
    -       8               4   Audio sampling frequency
    -      12             244   Reserved
    -     256               2   Aspect ratio: horizontal
    -     258               2   Aspect ratio: vertical
    -     260               1   Encoding quality used (0-255)
    -     261             123   Reserved
    -     384               4   Data size of the video block 0 (a multiple of 32)
    -     388               4   Data size of the video block 1 (a multiple of 32)
    -     392               4   Data size of the video block 2 (a multiple of 32, only with interlaced encoding)
    -     396               4   Data size of the video block 3 (a multiple of 32, only with interlaced encoding)
    -     400             112   Reserved
    -     512        Variable   Audio data
    - 4096xn0        Variable   Video block 0
    -   32xn1        Variable   Video block 1
    -   32xn2        Variable   Video block 2 (only with interlaced encoding)
    -   32xn3        Variable   Video block 3 (only with interlaced encoding)
    - 4096xn4               -   Beginning of the next A/V Frame
*/

static int earthsoft_read_header(AVFormatContext *s) {
    AVIOContext             *pb = s->pb;
    EarthsoftDVDemuxContext *c  = s->priv_data;

    /* make sure we start with the correct bytes */
    if (avio_r8(pb) != 'P' || avio_r8(pb) != 'V' || avio_r8(pb) != '3') {
        av_log(s, AV_LOG_ERROR,
               "Header doesn't contain valid start bytes (PV3)!\n");
        return AVERROR_INVALIDDATA;
    }

    /* read the standard stream information from the stream */
    c->codec_version = avio_r8(pb);
    if (c->codec_version != 2) {
        av_log(s, AV_LOG_ERROR, "Codec version %d is not supported!\n",
               c->codec_version);
        return AVERROR_PATCHWELCOME;
    }

    c->width            = avio_r8(pb) * 16;
    c->height           = avio_r8(pb) * 8;
    c->progressive_scan = (avio_r8(pb) & 0x1);

    av_log(s, AV_LOG_DEBUG, "Parsed video format information:\n"
                            "  Codec Version: %d\n"
                            "  Width: %"PRId64"\n"
                            "  Height: %"PRId64"\n"
                            "  Scan Type: %s\n",
           c->codec_version, c->width, c->height, c->progressive_scan ?
                                                 "progressive" : "interlaced");

    /* skip reserved bytes (aka padding) */
    avio_skip(pb, 249);

    /* read the luma quantizers */
    for (int i = 0; i < 64; i++) {
        c->luma_quantizers[i] = avio_rb16(pb);
    }

    /* read the chroma quantizers */
    for (int i = 0; i < 64; i++) {
        c->chroma_quantizers[i] = avio_rb16(pb);
    }

    /* skip another set of reserved bytes (aka padding) */
    avio_skip(pb, 15872);

    /* use the current position as the position where the data begins */
    c->data_offset = avio_tell(pb);

    /* create the hardcoded video and audio streams */
    create_video_stream(s);
    create_audio_stream(s);

    return 0;
}

static int earthsoft_read_packet(AVFormatContext *s, AVPacket *pkt) {
    return 0;
}

AVInputFormat ff_earthsoft_dv_demuxer = {
    .name           = "earthsoft_dv",
    .long_name      = NULL_IF_CONFIG_SMALL("Earthsoft PV3/PV4 DV video format"),
    .priv_data_size = sizeof(EarthsoftDVDemuxContext),
    .read_probe     = earthsoft_probe,
    .read_header    = earthsoft_read_header,
    .read_packet    = earthsoft_read_packet,
    .extensions     = "dv"
};
