/*
 * FFprobe
 * Copyright (c) 2007 Stefano Sabatini
 *
 * This file is *not* part of FFmpeg.
 *
 * FFprobe is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFprobe is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFprobe; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <libavformat/avformat.h>
#include <libavcodec/opt.h>

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>

#include "cmdutils.h"

static const char *program_name="FFprobe";
static const int program_birth_year = 2007;

/* todo
 * support for duration and bitrate computation
 */

/* globals */
const OptionDef options[];

/* FFprobe context */
int value_string_flags = 0;
int verbose = 1;
int do_show_tags = 0;
int do_show_files = 0;
int do_show_frames = 0;
int do_read_frames = 0;
int do_show_streams = 0;
int do_show_packets = 0;
int do_read_packets = 0;
int keep_going = 0;

typedef struct InputFilenameList {
    char* filename;
    struct InputFilenameList *next;
} InputFilenameList;

InputFilenameList *input_filenames = NULL;

typedef struct AVInputPacket {
    struct AVInputStream *ist;
    AVPacket *pkt;
    int64_t file_pkt_nb;
    int64_t stream_pkt_nb;
    /* these are used to tell what has been read from the packet,
     * when the packet is initialized they have to be set to pkt->data
     * and pkt->size respectively */
    int8_t *data_ptr;
    int data_size;
} AVInputPacket;

/* this struct contains all that show_frame needs to show the frame.
 * It contains data relative to the packet containing the frame,
 * and the data decoded (in data). This data has to be freed after the use. */
typedef struct AVInputFrame {
    struct AVInputStream *ist;
    int size;
    void *data;                 /* it can contain a picture, a samples buffer, a subtitle */
    int data_size;              /* the size of the data (if meaningful) */
    int64_t pkt_pts;
    int64_t pkt_dts;
    int pkt_size;
    int64_t pkt_duration;
    int64_t file_pkt_nb;
    int64_t stream_pkt_nb;
    int64_t stream_frame_nb;
    int64_t file_frame_nb;
    int pkt_flags;
} AVInputFrame;

/* modified version of the AVInputStream in ffmpeg.c */
typedef struct AVInputStream {
    struct AVInputFile *ifile; /* pointer to the input file, if any */
    AVStream *st;              /* pointer to the corresponding stream */
    AVCodec *decoder;
    int64_t duration;
    int64_t size;
    int64_t nb_pkts;
    int64_t nb_frames;
} AVInputStream;

typedef struct AVInputFile {
    AVFormatContext *demuxer_ctx;
    /* all the input stream of the input file are stored here */
    AVInputStream  **ist_table;
    AVInputStream  *cur_ist;
    AVPacket *cur_pkt;
    /* total number of frames in the file */
    int64_t  nb_frames;
    int64_t  nb_pkts;

    /* global effective size of the stream, has to be updated every read */
    /* it is different from the demuxer_ctx->file_size which is based on header info*/
    int64_t  size;
} AVInputFile;

char *binary_unit_prefixes[] = {
    "",
    "Ki",
    "Mi",
    "Gi",
    "Ti",
    "Pi"
};
static int binary_unit_prefixes_nb = sizeof (binary_unit_prefixes) / sizeof (char *);

char *decimal_unit_prefixes[] = {
    "",
    "K",
    "M",
    "G",
    "T",
    "P"
};
static int decimal_unit_prefixes_nb = sizeof (decimal_unit_prefixes) / sizeof(char *);

typedef enum UnitPrefixType {
    UNIT_PREFIX_TYPE_BINARY,
    UNIT_PREFIX_TYPE_DECIMAL
} UnitPrefixType;

typedef enum UnitId {
    UNIT_NONE,
    UNIT_SECOND,
    UNIT_HERTZ,
    UNIT_BYTE_PER_SECOND,
    UNIT_BIT_PER_SECOND,
    UNIT_BIT,
    UNIT_BYTE,
    UNIT_NB,
} UnitId;

typedef struct Unit {
    UnitId id;
    char *str;
} Unit;

Unit measure_units[UNIT_NB] = {
    [UNIT_NONE]            = { UNIT_NONE, "" },
    [UNIT_SECOND]          = { UNIT_SECOND, "s" },
    [UNIT_HERTZ]           = { UNIT_HERTZ, "Hz" },
    [UNIT_BIT]             = { UNIT_BIT, "bit" },
    [UNIT_BYTE]            = { UNIT_BYTE, "byte" },
    [UNIT_BYTE_PER_SECOND] = { UNIT_BYTE_PER_SECOND, "byte/s" },
    [UNIT_BIT_PER_SECOND]  = { UNIT_BIT_PER_SECOND, "bit/s" }
};

#define VALUE_STRING_USE_UNIT                   0x0001 /* print unit of measure */
#define VALUE_STRING_USE_PREFIX                 0x0002 /* print using prefixes */
#define VALUE_STRING_USE_BINARY_PREFIX          0x0004 /* use binary type prefixes, implies the use of prefixes */
#define VALUE_STRING_USE_BYTE_BINARY_PREFIX_BIGOTRY    0x0008 /* force binary type prefixes with bytes */
#define VALUE_STRING_USE_BABYLONIAN_TIME        0x0010 /* use babylonian chaotic notation for time (60 minute, 60 seconds and microseconds) */

/**
 * Prints in \p buf a prettified representation of the value in \p val,
 * using the unit of measure denoted in \p unit.
 * @param buf[in] Buffer where to write the string.
 * @param buf_size[in] Size of \p buf.
 * @return Pointer to \p buf where the string is putted.
 */
static char *value_string (char *buf, int buf_size, double val, UnitId unit, int flags) {
    int index;
    char *prefix_str;
    char *unit_str;

    if (unit == UNIT_BYTE && flags&VALUE_STRING_USE_BYTE_BINARY_PREFIX_BIGOTRY)
        flags |= VALUE_STRING_USE_BINARY_PREFIX;

    if (unit == UNIT_SECOND && flags & VALUE_STRING_USE_BABYLONIAN_TIME) {
        int hours, mins, secs, usecs;
        /* takes the 6 digits representing the microseconds and round */
        usecs= (int) (round((val - floor(val)) * 1000000));
        secs= (int) val;
        mins = secs / 60;
        secs %= 60;
        hours = mins / 60;
        mins %= 60;

        unit_str= flags & VALUE_STRING_USE_PREFIX ? measure_units[unit].str : "";
        snprintf(buf, buf_size, "%d:%02d:%02d.%06d %s", hours, mins, secs, usecs, unit_str);
        return buf;
    }

    if (flags & VALUE_STRING_USE_PREFIX) {
        if (flags & VALUE_STRING_USE_BINARY_PREFIX) {
            index = (int) (log2(val)) / 10;
            index = FFMAX(0, index); /* if the index is negative */
            index = FFMIN(index, binary_unit_prefixes_nb -1);
            val /= pow(2, index*10);
            prefix_str = binary_unit_prefixes[index];
        } else {
            index = (int) (log10(val)) / 3;
            index = FFMAX(0, index); /* if the index is negative */
            index = FFMIN(index, decimal_unit_prefixes_nb -1);
            val /= pow(10, index*3);
            prefix_str = decimal_unit_prefixes[index];
        }
    } else
        prefix_str = "";

    if (flags & VALUE_STRING_USE_UNIT)
        unit_str= measure_units[unit].str;
    else
        unit_str= "";

    if (flags & VALUE_STRING_USE_PREFIX)
        snprintf(buf, buf_size, "%.3f %s%s", val, prefix_str, unit_str);
    else
        snprintf(buf, buf_size, "%f %s", val, unit_str);

    return buf;
}

static char *time_value_string (char *buf, int buf_size, int64_t val, AVRational *time_base, int value_string_flags) {
    double d;

    if (val == AV_NOPTS_VALUE)
        snprintf(buf, buf_size, "N/A");
    else {
        d= (double) (val * time_base->num) / (double) time_base->den;
        value_string (buf, buf_size, d, UNIT_SECOND, value_string_flags);
    }
    return buf;
}

static char *ts_value_string (char *buf, int buf_size, int64_t ts) {
    if (ts == AV_NOPTS_VALUE)
        snprintf(buf, buf_size, "N/A");
    else {
        snprintf(buf, buf_size, "%"PRId64, ts);
    }
    return buf;
}

static const char *codec_type_strings[] = {
    [CODEC_TYPE_VIDEO]    = "video",
    [CODEC_TYPE_AUDIO]    = "audio",
    [CODEC_TYPE_DATA]     = "data",
    [CODEC_TYPE_SUBTITLE] = "subtitle",
    [CODEC_TYPE_NB]       = "unknown"
};

const char *codec_type_string (enum CodecType codec_type)
{
    if ((unsigned)codec_type>CODEC_TYPE_NB)
        codec_type = CODEC_TYPE_NB;
    return codec_type_strings[codec_type];
}

/* this is the same function used by M-player, whose returned value is
 * displayed when it's enabled the corresponding debugging option.
 *
 * Some codecs (for example rawvideo) don't store quality/quantization
 * information: in this case it returns simply -1.
 * TODO: I don't really understand this!! */
static double compute_average_mb_frame_quality (AVInputFrame *iframe) {
    AVFrame *frame = (AVFrame *)iframe->data;
    AVCodecContext *dec_ctx=iframe->ist->st->codec;
    double quality=0.0;
    int x, y, w, h;
    int8_t *q = frame->qscale_table;

    /* manage rawvideo and similiar cases */
    if (!q) {
        return -1.0;
    }

    w = ((dec_ctx->width  << dec_ctx->lowres)+15) >> 4;
    h = ((dec_ctx->height << dec_ctx->lowres)+15) >> 4;

    // average MB quantizer
    for (y=0; y<h; y++) {
        for (x=0; x<w; x++)
            quality += (double)*(q+x);
        q += frame->qstride;
    }
    quality /= w * h;

    return quality;
}

static void show_help () {
    printf("usage: ffprobe [options] infiles\n"
           "Simple Audio and Video prober\n");
    printf("\n");
    show_help_options(options, "Main options:\n", 1, 0);
}

/* print */
#define P(...) do { printf (__VA_ARGS__); } while(0)

static void show_stream (AVInputStream *ist) {
    AVStream *st=ist->st;
    AVCodecContext *dec_ctx=st->codec;
    AVCodec *dec=ist->decoder;
    char val_str[128];
    AVRational display_aspect_ratio;

    P("[STREAM]\n");
    /* information on the codec context */
    /* not every stream is bound to a decoder: for example streams for which
     * there isn't a corresponding decoder */
    if (dec) {
        P("codec_name=%s\n", dec->name);
        P("decoder_time_base=%d/%d\n", dec_ctx->time_base.num, dec_ctx->time_base.den);
        P("codec_type=%s\n", codec_type_string(ist->st->codec->codec_type));

        switch (dec_ctx->codec_type) {
        case CODEC_TYPE_VIDEO:
            P("r_frame_rate=%f\n", (double) st->r_frame_rate.num / st->r_frame_rate.den);
            P("r_frame_rate_num=%d\n", st->r_frame_rate.num);
            P("r_frame_rate_den=%d\n", st->r_frame_rate.den);
            P("width=%d\n", dec_ctx->width);
            P("height=%d\n", dec_ctx->height);
            P("gop_size=%d\n", dec_ctx->gop_size);
            P("has_b_frames=%d\n", dec_ctx->has_b_frames);
            P("sample_aspect_ratio=%d/%d\n", dec_ctx->sample_aspect_ratio.num, dec_ctx->sample_aspect_ratio.den);

            av_reduce(&display_aspect_ratio.num, &display_aspect_ratio.den,
                      dec_ctx->width*dec_ctx->sample_aspect_ratio.num,
                      dec_ctx->height*dec_ctx->sample_aspect_ratio.den,
                      1024*1024);
            P("display_aspect_ratio=%d/%d\n", display_aspect_ratio.num, display_aspect_ratio.den);

            P("pix_fmt=%s\n", avcodec_get_pix_fmt_name(dec_ctx->pix_fmt));
            break;

        case CODEC_TYPE_AUDIO:
            P("sample_rate=%s\n", value_string(val_str, sizeof(val_str),
                                               dec_ctx->sample_rate, UNIT_HERTZ, value_string_flags));
            P("channels=%d\n", dec_ctx->channels);
            P("bits_per_sample=%d\n", av_get_bits_per_sample (dec_ctx->codec_id));
            break;

        default:
            break;
        }
    } else {
        P("codec_type=unknown\n");
    }

    /* Information contained in AVStream */
    P("index=%d\n", st->index);
    P("time_base=%d/%d\n", st->time_base.num, st->time_base.den);
    if (st->language[0] != '\0')
        P("language=%s\n", st->language);

    /* start time present in the stream */
    P("start_time=%s\n", time_value_string(val_str, sizeof(val_str), st->start_time, &st->time_base, value_string_flags));
    P("duration=%s\n", time_value_string(val_str, sizeof(val_str), st->duration, &st->time_base, value_string_flags));

    P("nb_frames=%"PRId64"\n", st->nb_frames);

    /* informations in AVInputStream */
    if (do_read_packets) {
        P("probed_size=%s\n", value_string(val_str, sizeof(val_str),
                                           (double)ist->size, UNIT_BYTE, value_string_flags|VALUE_STRING_USE_BINARY_PREFIX));
        P("probed_nb_pkts=%"PRId64"\n", ist->nb_pkts);
        P("probed_nb_frames=%"PRId64"\n", ist->nb_frames);
    }
    P("[/STREAM]\n");
}

static void show_file (AVInputFile *ifile)
{
    AVFormatContext *demuxer_ctx = ifile->demuxer_ctx;
    char val_str[128];

    P("[FILE]\n");

    /* information in AVFormatContext */
    P("filename=%s\n", demuxer_ctx->filename);
    P("nb_streams=%d\n", demuxer_ctx->nb_streams);
    P("demuxer_name=%s\n", demuxer_ctx->iformat->name);
    if (demuxer_ctx->iformat->long_name)
        P("demuxer_long_name=%s\n", demuxer_ctx->iformat->long_name);

    if (demuxer_ctx->track != 0)
        P("track=%d\n", demuxer_ctx->track);
    if (demuxer_ctx->title[0] != '\0')
        P("title=%s\n", demuxer_ctx->title);
    if (demuxer_ctx->author[0] != '\0')
        P("author=%s\n", demuxer_ctx->author);
    if (demuxer_ctx->copyright[0] != '\0')
        P("copyright=%s\n", demuxer_ctx->copyright);
    if (demuxer_ctx->comment[0] != '\0')
        P("comment=%s\n", demuxer_ctx->comment);
    if (demuxer_ctx->album[0] != '\0')
        P("album=%s\n", demuxer_ctx->album);
    if (demuxer_ctx->year != 0)
        P("year=%d\n", demuxer_ctx->year);
    if (demuxer_ctx->genre[0] != '\0')
        P("genre=%s\n", demuxer_ctx->genre);

    /* all values in the demuxer context and in the AVInputFile are
     * expressed as AV_TIME_BASE units. */
/*     P("start_time=%f\n", (double) demuxer_ctx->start_time / AV_TIME_BASE); */

/*     /\* this values corresponds to the values stored in the header, or predicted since */
/*      * other header value, so isn't a reliable value. *\/ */
/*     P("duration=%f\n", (double) demuxer_ctx->duration / AV_TIME_BASE); */

    P("start_time=%s\n", time_value_string(val_str, sizeof(val_str), demuxer_ctx->start_time, &AV_TIME_BASE_Q, value_string_flags));
    P("duration=%s\n", time_value_string(val_str, sizeof(val_str), demuxer_ctx->duration, &AV_TIME_BASE_Q, value_string_flags));

    P("size=%s\n", value_string(val_str, sizeof(val_str),
                                (double)demuxer_ctx->file_size, UNIT_BYTE, value_string_flags));

    P("bit_rate=%s\n", value_string(val_str, sizeof(val_str),
                                    (double)demuxer_ctx->bit_rate, UNIT_BIT_PER_SECOND, value_string_flags));

    /* ifile probed values */
    if (do_read_packets) {
        P("probed_size=%s\n", value_string(val_str, sizeof(val_str),
                                            (double)ifile->size, UNIT_BYTE, value_string_flags));
        P("probed_nb_pkts=%"PRId64"\n", ifile->nb_pkts);
    }

    if (do_read_frames)
        P("probed_nb_frames=%"PRId64"\n", ifile->nb_frames);

    P("[/FILE]\n");
}

/**
 * Prints informations on the packet in input.
 * @param iframe[in] AVPacket struct to print out. It can't be NULL.
 */
static void show_packet (AVInputPacket *ipkt)
{
    char val_str[128];
    AVPacket *pkt = ipkt->pkt;
    AVInputStream *ist= ipkt->ist;

    /* all time values in a packet are expressed using the corresponding stream
     * time base units. */
    P("[PACKET]\n");
    P("codec_type=%s\n", codec_type_string(ist->st->codec->codec_type));
    P("stream_index=%d\n", pkt->stream_index);
    P("pts=%s\n", ts_value_string(val_str, sizeof(val_str), pkt->pts));
    P("pts_time=%s\n", time_value_string(val_str, sizeof(val_str), pkt->pts, &ist->st->time_base, value_string_flags));

    P("dts=%s\n", ts_value_string(val_str, sizeof(val_str), pkt->dts));
    P("dts_time=%s\n", time_value_string(val_str, sizeof(val_str), pkt->dts, &ist->st->time_base, value_string_flags));

    P("size=%s\n", value_string(val_str, sizeof(val_str),
                                (double)pkt->size, UNIT_BYTE, value_string_flags|VALUE_STRING_USE_BINARY_PREFIX));

    P("file_pkt_nb=%"PRId64"\n", ipkt->file_pkt_nb);
    P("stream_pkt_nb=%"PRId64"\n", ipkt->stream_pkt_nb);

    P("duration_ts=%s\n", ts_value_string(val_str, sizeof(val_str), pkt->duration));
    P("duration_time=%s\n", time_value_string(val_str, sizeof(val_str), pkt->duration, &ist->st->time_base, value_string_flags));

    P("flag_key=%c\n", (pkt->flags & PKT_FLAG_KEY) ? 'K' : '_');
    P("[/PACKET]\n");
}

/**
 * Prints informations about the frame in input.
 * @param iframe[in] AVInputFrame struct to print out. It must be a valid
 * iframe.
 */
static void show_frame (AVInputFrame *iframe) {
    AVInputStream *ist=iframe->ist;
    AVStream *st = iframe->ist->st;
    AVFrame *frame;             /* this is used if the frame is a video frame */
    char val_str[128];

    P("[FRAME]\n");
    P("codec_type=%s\n", codec_type_string(ist->st->codec->codec_type));

    switch (ist->st->codec->codec_type) {
    case CODEC_TYPE_VIDEO:
        frame = (AVFrame *)iframe->data;

        /* video frames has this too, what's its meaning? */
/*         P("frame_pts=%"PRId64"\n", frame->pts); */
        P ("pict_type=%c\n", av_get_pict_type_char(frame->pict_type));
/*         P ("pict_type=%c\n", av_get_pict_type_char(iframe->ist->st->codec->coded_frame->pict_type)); */

        /* quality is an integer varying from 1 to FF_LAMBDA_MAX */
        /* Mpeg video 1 and 2 and 4: varyies from 1 to 31, but */
        /* how it is computed? */
        P("quality=%d\n", frame->quality);
        /* this sometimes crashes!!! */
/*         P("average_mb_quality=%f\n", compute_average_mb_frame_quality(iframe)); */
        P("coded_picture_number=%d\n", frame->coded_picture_number);
        P("display_picture_number=%d\n", frame->display_picture_number);
        P("interlaced_frame=%d\n", frame->interlaced_frame);
        P("repeat_pict=%d\n", frame->repeat_pict);
        P("reference=%d\n", frame->reference);
        /* I don't know how to use it (but it would be useful)!!! */
/*         P("ref_index[]=%d,%d\n", (frame->ref_index[0]), (frame->ref_index[1])); */

        break;
    case CODEC_TYPE_AUDIO:
        P("samples_size=%s\n", value_string(val_str, sizeof(val_str),
                                            (double)iframe->data_size, UNIT_BYTE, value_string_flags));
        /* todo: add here an indication about the predicted duration, based on the
         * sample rate and the number of samples */
        break;

    default:
        break;
    }

    P("stream_index=%d\n", ist->st->index);
    P("size=%s\n", value_string(val_str, sizeof(val_str),
                                (double)iframe->size, UNIT_BYTE, value_string_flags));

    P("pkt_pts=%s\n", time_value_string(val_str, sizeof(val_str), iframe->pkt_pts, &st->time_base, value_string_flags));
    P("pkt_dts=%s\n", time_value_string(val_str, sizeof(val_str), iframe->pkt_dts, &st->time_base, value_string_flags));
    P("pkt_duration=%s\n",  time_value_string(val_str, sizeof(val_str), iframe->pkt_duration, &st->time_base, value_string_flags));

    P("file_pkt_nb=%"PRId64"\n",  iframe->file_pkt_nb);
    P("stream_pkt_nb=%"PRId64"\n", iframe->stream_pkt_nb);

    P("pkt_flag_key=%c\n", iframe->pkt_flags & PKT_FLAG_KEY ? 'K' : '_');

    P("[/FRAME]\n");
}

/**
 * Prints information regarding the tags contained in the multimedia container.
 * @warning Every container supports a different set of tags. FFmpeg libraries have
 * very limited tags understanding capabilities, and the tags displayed correspond to the
 * minimal set (ID3.1 tags) used by the MP3 standard.
 * @param mudem_context the container format context from which to read the tags.
 */
static void show_tags (const AVFormatContext *demuxer_ctx)
{
    P("[TAGS]\n");

    P("track=%d\n", demuxer_ctx->track);
    P("title=%s\n", demuxer_ctx->title);
    P("author=%s\n", demuxer_ctx->author);
    P("copyright=%s\n", demuxer_ctx->copyright);
    P("comment=%s\n", demuxer_ctx->comment);
    P("album=%s\n", demuxer_ctx->album);
    P("year=%d\n", demuxer_ctx->year);
    P("genre=%s\n", demuxer_ctx->genre);

    P("[/TAGS]\n");
}
#undef P

/**
 * This function is called when allocating the frame: in some cases
 * the frame isn't returned immediately, you have to wait for another
 * call to avcodec_decode_video(), but we need to fill the iframe
 * struct with actual informations (such as the packet informations).
 *
 * The opaque field of the codec context is used to pass the
 * corresponding input stream.
 *
 * @fixme get_buffer() isn't necessarily called for every codec, in this
 * case this simply can't work, and the opaque structure won't be
 * filled, which will end up in a crash (this happens for example with
 * rawdec). A new design is required!!
 */
static int my_get_buffer(struct AVCodecContext *codec_ctx, AVFrame *frame){
    AVInputStream *ist = (AVInputStream *) codec_ctx->opaque;
    AVPacket *cur_pkt;
    int ret= avcodec_default_get_buffer(codec_ctx, frame);
    AVInputFrame *iframe= av_malloc(sizeof(AVInputFrame));

    /* fill the iframe struct */
    cur_pkt= ist->ifile->cur_pkt;
    iframe->ist = ist;
    /* the size of a video frame is always the size of its packet */
    iframe->size = cur_pkt->size;
    iframe->pkt_pts =       cur_pkt->pts;
    iframe->pkt_dts =       cur_pkt->dts;
    iframe->pkt_size =      cur_pkt->size;
    iframe->pkt_duration =  cur_pkt->duration;
    iframe->pkt_flags =     cur_pkt->flags;
    iframe->file_pkt_nb =   ist->ifile->nb_pkts;
    iframe->stream_pkt_nb = ist->nb_pkts;
    iframe->data = frame;

#if 0
    fprintf (stderr, "Allocating video frame from stream packet number %"PRId64"\n", ist->nb_pkts);
#endif

    /* store in the frame itself the pointer to this struct.  When
     * avcodec_decode_frame will return the got frame, the iframe data will
     * be accessible through frame->opaque. */
    frame->opaque= iframe;
    return ret;
}

static void my_release_buffer(struct AVCodecContext *codec_ctx, AVFrame *frame){
    AVInputFrame *iframe= (AVInputFrame *)frame->opaque;

#if 0
    fprintf (stderr, "Releasing video frame from packet number %"PRId64"\n", iframe->stream_pkt_nb);
#endif

/*     if(frame) av_freep(&frame->opaque); */
    avcodec_default_release_buffer(codec_ctx, frame);
}

/**
 * Decodes a packet, eventually getting out from it a frame.  A packet
 * (for example an audio frame) may contain several frames.  If you
 * want to extract *all* the frame from the input packet, then you
 * should call the function many times, up to when it gives error (a
 * negative value) decoding the packet or it return NULL.  This also
 * means that it has to keep a static state, to keepbooking where is
 * left to decode on the packet.
 *
 * @param ifile[in] AVInputFile to which belongs the packets.
 * @param pkt[in] Pointer to the packet to decode.
 * @param got_iframe[in,out] Put here the pointer to the decoded frame
 * extracted from the packet, NULL, if it can't extract nothing.
 * @return Integer containing the decoded data, 0 if it doesn't
 * decoded nothing (for example because the data in the packet has
 * been entirely decoded), or a negative value in case of error.
 */
/* todo: interface simplification: ifile + pkt -> ipkt only arg */
static int decode_packet (AVInputFile *ifile, AVPacket *pkt, AVInputFrame **got_iframe, int *pkt_is_new) {
    int ret, got_frame;
    AVFrame picture;
    AVSubtitle *subtitle;
    static unsigned int samples_buf_size= 0;
    static int16_t *samples_buf= NULL;
    AVInputStream *ist;
    static uint8_t *pkt_data_ptr = NULL;
    static int pkt_data_len = 0;
    AVInputFrame *iframe;

    /* if the packet is new */
    if (*pkt_is_new) {
        pkt_data_ptr = pkt->data;
        pkt_data_len = pkt->size;
        *pkt_is_new = 0;
    }

    ist = ifile->ist_table[pkt->stream_index];

    /* decode the data left in the packet, put the return value in ret and the frame in got_iframe */
    switch(ist->st->codec->codec_type) {
    case CODEC_TYPE_AUDIO:
        /* reallocate samples and put in the samples_buf_size the size of the allocated memory */
        samples_buf= av_fast_realloc(samples_buf, &samples_buf_size, FFMAX(pkt->size*sizeof(*samples_buf), AVCODEC_MAX_AUDIO_FRAME_SIZE));
        if (!samples_buf)
            fprintf(stderr, "something bad is going to happen...\n");
        /* put in samples_buf the decoded frame samples; ret contains the size of the data used (compressed frame size) */
        ret = avcodec_decode_audio2(ist->st->codec, samples_buf, &samples_buf_size, pkt_data_ptr, pkt_data_len);
        if (ret < 0)
            break;
        pkt_data_ptr += ret;
        pkt_data_len -= ret;
        /* what happens if samples_size <= 0? */

        /* that should be the very same as ret > 0; if ret == 0 means that no
         * decoding occurred, that is the packet is exhausted */
        if (samples_buf_size > 0) {
            /* allocate an AVInputFrame and fills with the appropriate data */
            iframe = (AVInputFrame *) av_malloc (sizeof (AVInputFrame));
            iframe->size = ret;
            iframe->ist = ist;
            iframe->pkt_pts = pkt->pts;
            iframe->pkt_dts = pkt->dts;
            iframe->pkt_size = pkt->size;
            iframe->pkt_duration = pkt->duration;
            iframe->pkt_flags = pkt->flags;
            iframe->file_pkt_nb = ifile->nb_pkts;
            iframe->stream_pkt_nb = ist->nb_pkts;
            iframe->data = (void *)samples_buf;
            iframe->data_size = samples_buf_size;
            *got_iframe = iframe;
        } else {
            *got_iframe = NULL;
        }
        break;

    case CODEC_TYPE_VIDEO:
        /* is assumed a YUV420 format for the decoded data */
/*         picture_size = (ist->st->codec->width * ist->st->codec->height * 3) / 2; */
        /* if got a frame then put in picture the decoded frame, and set got_frame to a positive value */
        got_frame = 0;
        ret = avcodec_decode_video(ist->st->codec, &picture, &got_frame, pkt_data_ptr, pkt_data_len);
        /* av_codec_decode_video automatically allocates an AVInputFrame in my_get_buffer */
        if (ret < 0)
            break;
        if (got_frame) {
            /* packet informations are already stored in the picture (see get_my_buffer) */
            iframe = (AVInputFrame *)picture.opaque;
            /* iframe->data = &picture; /\* dog which bites its own tail *\/ */
            *got_iframe = iframe;
        }
        /* avcodec_decode_video slurps all the data at once */
        pkt_data_len = 0;
        break;

    case CODEC_TYPE_SUBTITLE:
        subtitle = av_malloc (sizeof(AVSubtitle));
        ret = avcodec_decode_subtitle(ist->st->codec, subtitle, &got_frame, pkt_data_ptr, pkt_data_len);
        if (ret < 0)
            break;

        if (got_frame) {
            iframe = (AVInputFrame *) av_malloc (sizeof (AVInputFrame));
            iframe->data = (void *)subtitle;
            iframe->ist = ist;
            iframe->size = ret;
            iframe->pkt_pts = pkt->pts;
            iframe->pkt_dts = pkt->dts;
            iframe->pkt_duration = pkt->duration;
            iframe->pkt_flags = pkt->flags;
            iframe->file_pkt_nb = ifile->nb_pkts;
            iframe->stream_pkt_nb = ist->nb_pkts;
            *got_iframe = iframe;
        }
        /* I'm supposing it works like avcodec_decode_video, slurping it all the packet data*/
        pkt_data_len = 0;
        break;

    default:
        fprintf (stderr, "unsupported type of packet");
        ret = -1;
        break;
    }

    /* reset the state of the function if the packet is finished */
/*     if (pkt_data_len == 0) { */
/*         is_new_packet = 1; */
/*         pkt_data_ptr = NULL; */
/*         pkt_data_len = 0; */
/*     } */
    return ret;
}

/**
 * Frees the memory allocated by the frame.
 */
static void av_input_frame_free (AVInputFrame *iframe) {
    switch (iframe->ist->st->codec->codec_type) {
    case CODEC_TYPE_VIDEO:
        avpicture_free ((AVPicture *)iframe->data);
        av_freep(&iframe->data);
        break;

    case CODEC_TYPE_AUDIO:
        av_freep(&iframe->data);
        break;

    case CODEC_TYPE_SUBTITLE:
        av_freep(&iframe->data);
        break;

    default:
        break;
    }

    av_freep(&iframe);
}

static void read_packets (AVInputFile *ifile) {
    AVInputStream *ist;
    AVInputFrame *got_iframe=NULL;
    AVPacket pkt;
    AVInputPacket ipkt;
    int pkt_is_new;

    av_init_packet(&pkt);

    while (!av_read_frame(ifile->demuxer_ctx, &pkt)) {
        ist= ifile->ist_table[pkt.stream_index];

        /* UPDATE INPUT FILE */
        ifile->size += pkt.size;
        ifile->cur_pkt = &pkt;
        ifile->nb_pkts++;
        ifile->cur_ist=ist;

        /* UPDATE INPUT STREAM */
        ist->size += pkt.size;
        ist->nb_pkts++;

        /* UPDATE INPUT PACKET */
        ipkt.ist = ist;
        ipkt.pkt = &pkt;
        ipkt.stream_pkt_nb = ist->nb_pkts;
        ipkt.file_pkt_nb = ifile->nb_pkts;
        ipkt.data_ptr = pkt.data;
        ipkt.data_size = pkt.size;

        if (do_show_packets)
            show_packet(&ipkt);

        /* decode the frame and set the corresponding cur_iframe struct for it */
        if (ist->decoder && do_read_frames) {
            pkt_is_new=1;
            if (decode_packet(ifile, &pkt, &got_iframe, &pkt_is_new) > 0 || got_iframe) {
                if (got_iframe) {
                    ifile->nb_frames++;
                    ist->nb_frames++;
                    got_iframe->file_frame_nb= ifile->nb_frames;
                    got_iframe->stream_frame_nb= ist->nb_frames;

                    if (do_show_frames) show_frame(got_iframe);
/*                     av_input_frame_free (got_iframe); */
                }
                got_iframe = NULL;
            }
        }
        av_free_packet(&pkt);
    }

    /* flush every frame still to be decoded in the streams */
/*     for (i=0; i< ifile->demuxer_ctx->nb_streams; i++) { */
/*         pkt.stream_index = i; */
/*         pkt.data = NULL; */
/*         pkt.size = 0; */
/*         do { */
/*             pkt_is_new=1; */
/*             while (decode_packet(ifile, &pkt, got_iframe_ptr, &pkt_is_new) >= 0) */
/*                 if (*got_iframe_ptr) { */
/*                     ifile->nb_frames++; */
/*                     ist->nb_frames++; */
/*                     (*got_iframe_ptr)->file_frame_nb= ifile->nb_frames; */
/*                     (*got_iframe_ptr)->stream_frame_nb= ist->nb_frames; */
/*                     if (do_show_frames) show_frame(*got_iframe_ptr); */
/*                     av_input_frame_free (*got_iframe_ptr); */
/*                 } */
/*         } while (got_iframe_ptr); */
/*     } */
}

static int open_input_file (AVInputFile **ifile_ptr, const char *filename) {
    int err, i;
    AVInputStream **ist_table, *ist;
    AVInputFile *ifile;
    AVCodec *codec;
    AVFormatContext *demuxer_ctx;

    /* open the file and initialize the format context */
    ifile = (AVInputFile *)av_malloc (sizeof (AVInputFile));
    demuxer_ctx = av_alloc_format_context();
    ifile->size = 0;

    err = av_open_input_file(&demuxer_ctx, filename, NULL, 0, NULL);
    if (err < 0) {
        print_error(filename, err);
        return(err);
    }

    /* fill the streams in the demuxer context */
    err = av_find_stream_info(demuxer_ctx);
    if (err < 0) {
        print_error(filename, err);
        return(err);
    }
    ifile->demuxer_ctx = demuxer_ctx;

    /* dump info */
    dump_format(ifile->demuxer_ctx, 0, filename, 0);

    /* create the table of the input files */
    ist_table = av_mallocz(demuxer_ctx->nb_streams * sizeof(AVInputStream *));
    if (!ist_table) {
        print_error(filename, AVERROR(ENOMEM));
        exit(1);
    }

    /* create the various input_streams */
    for(i=0; i<demuxer_ctx->nb_streams; i++) {
        ist = av_mallocz(sizeof(AVInputStream));
        if (!ist) {
            print_error(filename, AVERROR(ENOMEM));
            exit(1);
        }
        ist_table[i] = ist;
    }
    ifile->ist_table=ist_table;

    /* initialize all the input streams */
    for(i=0; i< demuxer_ctx->nb_streams; i++) {
        ist = ist_table[i];
        ist->ifile = ifile;
        ist->st = demuxer_ctx->streams[i];
        /* put a pointer to itself in the decoder context (this is used in my_get_buffer)  */
        ist->st->codec->opaque = (void *)ist; /* now the codec context knows its ist */
        ist->nb_frames = 0;
        ist->nb_pkts = 0;
        /* global size of all the frames extracted from the stream (again different
         * from the global size of all the frames extracted from the demuxer */
        ist->size = 0;
    }

    /* bound to each input stream a corresponding decoder */
    /* open each decoder */
    for(i=0; i<demuxer_ctx->nb_streams; i++) {
        ist = ist_table[i];

        /* even in the case one stream can't be decoded, ffprobe goes on */
        codec = avcodec_find_decoder(ist->st->codec->codec_id);
        if (!codec) {
            fprintf(stderr, "Unsupported codec (id=%d) for input stream %d\n",
                    ist->st->codec->codec_id, ist->st->index);
            codec=NULL;
        } else if (avcodec_open(ist->st->codec, codec) < 0) {
            fprintf(stderr, "Error while opening codec for input stream %d\n",
                    ist->st->index);
            codec=NULL;
        }
        ist->decoder=codec;
        /* set in the codec context the customized {get,release}_buffer functions */
        ist->st->codec->get_buffer= my_get_buffer;
        ist->st->codec->release_buffer= my_release_buffer;
    }

    *ifile_ptr = ifile;
    return 0;
}

static int probe_file (const char *filename) {
    AVInputStream *ist;
    AVInputFile *ifile;
    int ret, i;

    /* open and allocate the AVInputFile struct and the AVInputStream table */
    ret = open_input_file (&ifile, filename);
    if (ret) {
        return ret;
    }

    if (do_show_tags)
        show_tags(ifile->demuxer_ctx);

    if (do_read_packets)
        read_packets(ifile);

    if (do_show_streams)
        for(i=0; i< ifile->demuxer_ctx->nb_streams; i++) {
            ist = ifile->ist_table[i];
            show_stream (ist);
        }

    if (do_show_files)
        show_file(ifile);

    /* close input file */
    av_close_input_file(ifile->demuxer_ctx);
    return 0;
}

static void opt_pretty(void) {
    value_string_flags =
        VALUE_STRING_USE_UNIT |
        VALUE_STRING_USE_PREFIX |
        VALUE_STRING_USE_BABYLONIAN_TIME |
        VALUE_STRING_USE_BYTE_BINARY_PREFIX_BIGOTRY;
}

static void opt_read_frames(void) {
    /* to read frames imply to *read* packets*/
    do_read_frames = do_read_packets = 1;
}

static void opt_show_frames(void) {
    /* to show frames imply to *read* frames */
    opt_read_frames();
    do_show_frames = 1;
}

static void opt_show_help(void) {
    show_help(stdout);
    exit(0);
}

static void opt_show_license(void)
{
    show_license(program_name);
    exit(0);
}

static void opt_show_packets(void) {
    do_show_packets = 1;
    /* to show frames imply to *read* packets */
    do_read_packets = 1;
}

static void opt_show_version(void) {
    show_version(program_name);
    exit(0);
}

static void opt_verbose(const char *arg)
{
    verbose = atoi(arg);
    av_log_level = atoi(arg);
}

static void opt_input_file (const char *filename) {
    InputFilenameList *last, *elt=NULL;

    if (!strcmp(filename, "-"))
        filename = "pipe:";

    /* append this element to the end of input_filenames */
    elt = av_malloc(sizeof(InputFilenameList));
    elt->filename = av_strdup (filename);
    elt->next = NULL;

    /* first one? */
    if (!input_filenames) {
        input_filenames = elt;
    } else {
        /* append to the list */
        for (last = input_filenames; last->next; last = last->next)
            ;
        last->next = elt;
    }
}

/* OptionDef options system */
const OptionDef options[] = {
    /* main options */
    { "L", 0, {(void*)opt_show_license}, "show license" },
    { "version", 0, {(void*)opt_show_version}, "show version" },
    { "h", 0, {(void*)opt_show_help}, "show help" },
    { "k", OPT_BOOL, {(void*)&keep_going}, "keep going even in case of error, always returns 0" },
    { "pretty", 0, {(void*)opt_pretty}, "pretty print numerical values, more human readable" },
    { "read_packets", OPT_BOOL, {(void*)&do_read_packets}, "read packets info" },
    { "read_frames", 0, {(void*)opt_read_frames}, "read frames info" },
    { "show_files", OPT_BOOL, {(void*)&do_show_files}, "show file info" },
    { "show_frames", 0, {(void*)opt_show_frames}, "show frames info, implies the option -read_frames and -read_packets" },
    { "show_packets", 0, {(void*)opt_show_packets}, "show packets info, implies the option -read_packets" },
    { "show_streams", OPT_BOOL, {(void*)&do_show_streams}, "show streams info" },
    { "show_tags", OPT_BOOL, {(void*)&do_show_tags}, "show tags info" },
    { "v", HAS_ARG, {(void*)opt_verbose}, "control amount of logging", "verbose" },
    { NULL, },
};

#define DEBUG_PRETTY_PRINTER 0

#if DEBUG_PRETTY_PRINTER
int main(int argc, char **argv) {
    char buf[128];
    int i;
    double d[] = { 1, 10, 100, 1000, 1010, 1024, 2048, 1000000, 121928080982.8281 };
    int n;

    n=sizeof(d) / sizeof(double);

    for (i=0; i < n; i++)
        printf ("%f=%s\n", d[i], value_string(buf, sizeof(buf), d[i], UNIT_NONE, VALUE_STRING_USE_PREFIX));

    printf ("Using binary prefix...\n");
    for (i=0; i < n; i++)
        printf ("%f=%s\n", d[i], value_string(buf, sizeof(buf), d[i], UNIT_NONE, VALUE_STRING_USE_BINARY_PREFIX));
    exit(0);
}
#else
int main(int argc, char **argv) {
    InputFilenameList *elt;
    int ret;

    av_register_all();

    show_banner(program_name, program_birth_year);

    /* parsing stuff, this prepares the global ffprobe context */
    parse_options(argc, argv, options, opt_input_file);

    if (!input_filenames) {
        fprintf(stderr, "You have to specify at least one input file.\n");
        show_help(stderr);
        exit(1);
    }

    /* process every single file in input_filenames */
    for (elt = input_filenames; elt; elt = elt->next) {
/*         fprintf(stderr, "filename: %s\n", elt->filename); */
        ret = probe_file (elt->filename);
        if (ret != 0 && !keep_going)
           break;
    }

    /* always return 0 with keep going enabled */
    if (keep_going)
        ret = 0;
    exit(ret);
}
#endif
