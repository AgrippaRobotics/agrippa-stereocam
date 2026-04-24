/*
 * video_writer.c — MP4 video recording for ag-cam-tools
 *
 * Wraps FFmpeg libavcodec / libavformat / libswscale to encode
 * RGB24 stereo frame pairs into H.264 MP4 files.
 */

#include "video_writer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>

/* ------------------------------------------------------------------------ */
/*  Single-stream encoder state                                              */
/* ------------------------------------------------------------------------ */

typedef struct {
    AVFormatContext *fmt_ctx;
    AVCodecContext  *codec_ctx;
    AVStream        *stream;
    struct SwsContext *sws;
    AVFrame         *frame;     /* YUV420P working frame */
    AVPacket        *pkt;
    int64_t          pts;
} AgEncoder;

/* ------------------------------------------------------------------------ */
/*  Writer                                                                   */
/* ------------------------------------------------------------------------ */

struct AgVideoWriter {
    AgVideoLayout layout;
    int           eye_w;
    int           eye_h;
    int64_t       frame_count;

    /* Assembly buffer for SBS / TB compositing (pre-allocated). */
    uint8_t      *composite_buf;
    int           composite_w;
    int           composite_h;

    /* One encoder for SBS / TB, two for SEPARATE. */
    AgEncoder     enc[2];
    int           n_encoders;
};

/* ------------------------------------------------------------------------ */
/*  Layout parsing                                                           */
/* ------------------------------------------------------------------------ */

int
ag_video_layout_parse (const char *str, AgVideoLayout *out)
{
    if (!str || !out) return -1;
    if (strcmp (str, "sbs") == 0 || strcmp (str, "side-by-side") == 0)
        { *out = AG_VIDEO_LAYOUT_SBS; return 0; }
    if (strcmp (str, "tb") == 0 || strcmp (str, "top-bottom") == 0)
        { *out = AG_VIDEO_LAYOUT_TOP_BOTTOM; return 0; }
    if (strcmp (str, "separate") == 0)
        { *out = AG_VIDEO_LAYOUT_SEPARATE; return 0; }
    if (strcmp (str, "mono") == 0)
        { *out = AG_VIDEO_LAYOUT_MONO; return 0; }
    return -1;
}

/* ------------------------------------------------------------------------ */
/*  Encoder helpers                                                          */
/* ------------------------------------------------------------------------ */

static const AVCodec *
find_h264_encoder (void)
{
#ifdef __APPLE__
    const AVCodec *vtb = avcodec_find_encoder_by_name ("h264_videotoolbox");
    if (vtb) return vtb;
#endif
    return avcodec_find_encoder (AV_CODEC_ID_H264);
}

/* Round width/height up to even (H.264 requires even dimensions). */
static int
round_even (int v)
{
    return (v + 1) & ~1;
}

static int
encoder_open (AgEncoder *enc, const char *path, int width, int height,
              double fps, const AVCodec *codec)
{
    int ret;

    memset (enc, 0, sizeof *enc);

    /* Output format context. */
    ret = avformat_alloc_output_context2 (&enc->fmt_ctx, NULL, NULL, path);
    if (ret < 0 || !enc->fmt_ctx) {
        fprintf (stderr, "error: could not create output context for %s\n",
                 path);
        return -1;
    }

    /* Stream. */
    enc->stream = avformat_new_stream (enc->fmt_ctx, NULL);
    if (!enc->stream) {
        fprintf (stderr, "error: could not create video stream\n");
        goto fail;
    }

    /* Codec context. */
    enc->codec_ctx = avcodec_alloc_context3 (codec);
    if (!enc->codec_ctx) {
        fprintf (stderr, "error: could not allocate codec context\n");
        goto fail;
    }

    enc->codec_ctx->width     = width;
    enc->codec_ctx->height    = height;
    enc->codec_ctx->pix_fmt   = AV_PIX_FMT_YUV420P;
    enc->codec_ctx->time_base = (AVRational){ 1, (int) (fps * 1000) };
    enc->stream->time_base    = enc->codec_ctx->time_base;

    /* Try to hint for quality. CRF-like for software x264, bitrate for HW. */
    if (strcmp (codec->name, "h264_videotoolbox") == 0) {
        /* VideoToolbox: use a reasonable constant-quality bitrate. */
        enc->codec_ctx->bit_rate = (int64_t) width * height * 4;
        av_opt_set (enc->codec_ctx->priv_data, "realtime", "true", 0);
    } else {
        /* Software x264: CRF 18. */
        av_opt_set (enc->codec_ctx->priv_data, "crf", "18", 0);
        av_opt_set (enc->codec_ctx->priv_data, "preset", "fast", 0);
    }

    if (enc->fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        enc->codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    ret = avcodec_open2 (enc->codec_ctx, codec, NULL);
    if (ret < 0) {
        fprintf (stderr, "error: could not open codec %s: %d\n",
                 codec->name, ret);
        goto fail;
    }

    ret = avcodec_parameters_from_context (enc->stream->codecpar,
                                            enc->codec_ctx);
    if (ret < 0) {
        fprintf (stderr, "error: could not copy codec parameters\n");
        goto fail;
    }

    /* Color-space converter: RGB24 → YUV420P. */
    enc->sws = sws_getContext (width, height, AV_PIX_FMT_RGB24,
                               width, height, AV_PIX_FMT_YUV420P,
                               SWS_BILINEAR, NULL, NULL, NULL);
    if (!enc->sws) {
        fprintf (stderr, "error: could not create sws context\n");
        goto fail;
    }

    /* Working frame. */
    enc->frame = av_frame_alloc ();
    if (!enc->frame) goto fail;
    enc->frame->format = AV_PIX_FMT_YUV420P;
    enc->frame->width  = width;
    enc->frame->height = height;
    ret = av_frame_get_buffer (enc->frame, 0);
    if (ret < 0) {
        fprintf (stderr, "error: could not allocate frame buffer\n");
        goto fail;
    }

    enc->pkt = av_packet_alloc ();
    if (!enc->pkt) goto fail;

    /* Open output file. */
    if (!(enc->fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open (&enc->fmt_ctx->pb, path, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf (stderr, "error: could not open output file %s\n", path);
            goto fail;
        }
    }

    ret = avformat_write_header (enc->fmt_ctx, NULL);
    if (ret < 0) {
        fprintf (stderr, "error: could not write header to %s\n", path);
        goto fail;
    }

    enc->pts = 0;
    return 0;

fail:
    if (enc->pkt) av_packet_free (&enc->pkt);
    if (enc->frame) av_frame_free (&enc->frame);
    if (enc->sws) sws_freeContext (enc->sws);
    if (enc->codec_ctx) avcodec_free_context (&enc->codec_ctx);
    if (enc->fmt_ctx) {
        if (enc->fmt_ctx->pb &&
            !(enc->fmt_ctx->oformat->flags & AVFMT_NOFILE))
            avio_closep (&enc->fmt_ctx->pb);
        avformat_free_context (enc->fmt_ctx);
    }
    memset (enc, 0, sizeof *enc);
    return -1;
}

static int
encoder_write_rgb (AgEncoder *enc, const uint8_t *rgb, int width,
                   double fps)
{
    int ret;

    ret = av_frame_make_writable (enc->frame);
    if (ret < 0) return -1;

    /* Convert RGB24 → YUV420P. */
    const uint8_t *src_data[1] = { rgb };
    int src_linesize[1]        = { width * 3 };
    sws_scale (enc->sws, src_data, src_linesize, 0,
               enc->frame->height, enc->frame->data,
               enc->frame->linesize);

    enc->frame->pts = enc->pts;
    enc->pts += (int) (fps * 1000.0 / fps);  /* = 1000 per frame */

    /* Encode. */
    ret = avcodec_send_frame (enc->codec_ctx, enc->frame);
    if (ret < 0) return -1;

    while (ret >= 0) {
        ret = avcodec_receive_packet (enc->codec_ctx, enc->pkt);
        if (ret == AVERROR (EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0) return -1;

        av_packet_rescale_ts (enc->pkt, enc->codec_ctx->time_base,
                              enc->stream->time_base);
        enc->pkt->stream_index = enc->stream->index;

        ret = av_interleaved_write_frame (enc->fmt_ctx, enc->pkt);
        if (ret < 0) return -1;
    }

    return 0;
}

static int
encoder_flush (AgEncoder *enc)
{
    int ret;

    if (!enc->fmt_ctx) return 0;

    /* Send NULL frame to flush. */
    ret = avcodec_send_frame (enc->codec_ctx, NULL);
    if (ret < 0 && ret != AVERROR_EOF) return -1;

    while (1) {
        ret = avcodec_receive_packet (enc->codec_ctx, enc->pkt);
        if (ret == AVERROR_EOF || ret == AVERROR (EAGAIN))
            break;
        if (ret < 0) return -1;

        av_packet_rescale_ts (enc->pkt, enc->codec_ctx->time_base,
                              enc->stream->time_base);
        enc->pkt->stream_index = enc->stream->index;
        av_interleaved_write_frame (enc->fmt_ctx, enc->pkt);
    }

    return av_write_trailer (enc->fmt_ctx);
}

static void
encoder_free (AgEncoder *enc)
{
    if (!enc->fmt_ctx) return;

    if (enc->pkt) av_packet_free (&enc->pkt);
    if (enc->frame) av_frame_free (&enc->frame);
    if (enc->sws) sws_freeContext (enc->sws);
    if (enc->codec_ctx) avcodec_free_context (&enc->codec_ctx);

    if (enc->fmt_ctx->pb &&
        !(enc->fmt_ctx->oformat->flags & AVFMT_NOFILE))
        avio_closep (&enc->fmt_ctx->pb);
    avformat_free_context (enc->fmt_ctx);
    memset (enc, 0, sizeof *enc);
}

/* ------------------------------------------------------------------------ */
/*  Path helpers for SEPARATE layout                                         */
/* ------------------------------------------------------------------------ */

static char *
make_separate_path (const char *path, const char *suffix)
{
    /* Insert suffix before extension: "/dir/foo.mp4" → "/dir/foo_left.mp4" */
    const char *dot = strrchr (path, '.');
    size_t stem_len = dot ? (size_t)(dot - path) : strlen (path);
    const char *ext = dot ? dot : ".mp4";
    size_t suf_len  = strlen (suffix);
    size_t ext_len  = strlen (ext);

    char *out = malloc (stem_len + suf_len + ext_len + 1);
    if (!out) return NULL;
    memcpy (out, path, stem_len);
    memcpy (out + stem_len, suffix, suf_len);
    memcpy (out + stem_len + suf_len, ext, ext_len);
    out[stem_len + suf_len + ext_len] = '\0';
    return out;
}

/* ------------------------------------------------------------------------ */
/*  Public API                                                               */
/* ------------------------------------------------------------------------ */

AgVideoWriter *
ag_video_writer_new (const char *path, int eye_w, int eye_h,
                      double fps, AgVideoLayout layout)
{
    const AVCodec *codec = find_h264_encoder ();
    if (!codec) {
        fprintf (stderr, "error: no H.264 encoder available\n");
        return NULL;
    }

    AgVideoWriter *w = calloc (1, sizeof *w);
    if (!w) return NULL;

    w->layout  = layout;
    w->eye_w   = eye_w;
    w->eye_h   = eye_h;

    switch (layout) {
    case AG_VIDEO_LAYOUT_SBS:
        w->composite_w = round_even (eye_w * 2);
        w->composite_h = round_even (eye_h);
        w->n_encoders  = 1;
        break;
    case AG_VIDEO_LAYOUT_TOP_BOTTOM:
        w->composite_w = round_even (eye_w);
        w->composite_h = round_even (eye_h * 2);
        w->n_encoders  = 1;
        break;
    case AG_VIDEO_LAYOUT_SEPARATE:
        w->composite_w = round_even (eye_w);
        w->composite_h = round_even (eye_h);
        w->n_encoders  = 2;
        break;
    case AG_VIDEO_LAYOUT_MONO:
        w->composite_w = round_even (eye_w);
        w->composite_h = round_even (eye_h);
        w->n_encoders  = 1;
        break;
    }

    /* Composite assembly buffer is only needed for SBS / TB compositing.
     * SEPARATE and MONO encode their inputs directly. */
    if (layout == AG_VIDEO_LAYOUT_SBS ||
        layout == AG_VIDEO_LAYOUT_TOP_BOTTOM) {
        w->composite_buf = malloc ((size_t) w->composite_w *
                                   w->composite_h * 3);
        if (!w->composite_buf) {
            free (w);
            return NULL;
        }
    }

    /* Open encoder(s). */
    if (layout == AG_VIDEO_LAYOUT_SEPARATE) {
        char *left_path  = make_separate_path (path, "_left");
        char *right_path = make_separate_path (path, "_right");
        if (!left_path || !right_path) {
            free (left_path);
            free (right_path);
            free (w);
            return NULL;
        }
        int ok = 0;
        ok |= encoder_open (&w->enc[0], left_path, w->composite_w,
                             w->composite_h, fps, codec);
        if (ok == 0)
            ok |= encoder_open (&w->enc[1], right_path, w->composite_w,
                                 w->composite_h, fps, codec);
        free (left_path);
        free (right_path);
        if (ok != 0) {
            encoder_free (&w->enc[0]);
            encoder_free (&w->enc[1]);
            free (w);
            return NULL;
        }
    } else {
        if (encoder_open (&w->enc[0], path, w->composite_w,
                          w->composite_h, fps, codec) != 0) {
            free (w->composite_buf);
            free (w);
            return NULL;
        }
    }

    const char *layout_name =
        layout == AG_VIDEO_LAYOUT_SBS        ? "sbs"        :
        layout == AG_VIDEO_LAYOUT_TOP_BOTTOM ? "top-bottom" :
        layout == AG_VIDEO_LAYOUT_SEPARATE   ? "separate"   :
        layout == AG_VIDEO_LAYOUT_MONO       ? "mono"       : "?";

    printf ("Recording to %s (%dx%d, %s, %.1f fps, codec %s)\n",
            path, w->composite_w, w->composite_h, layout_name,
            fps, codec->name);

    return w;
}

int
ag_video_writer_add_frame (AgVideoWriter *w,
                            const uint8_t *left_rgb,
                            const uint8_t *right_rgb)
{
    if (!w) return -1;

    int ret = 0;
    double fps = 1.0;  /* pts increment is fixed at 1000 in encoder */

    switch (w->layout) {
    case AG_VIDEO_LAYOUT_SBS: {
        /* Copy left and right side by side into composite buffer. */
        int ew3 = w->eye_w * 3;
        int cw3 = w->composite_w * 3;
        for (int y = 0; y < w->eye_h; y++) {
            memcpy (w->composite_buf + y * cw3,
                    left_rgb + y * ew3, ew3);
            memcpy (w->composite_buf + y * cw3 + ew3,
                    right_rgb + y * ew3, ew3);
        }
        /* Zero-fill padding columns if composite_w > eye_w * 2. */
        if (w->composite_w > w->eye_w * 2) {
            int pad = (w->composite_w - w->eye_w * 2) * 3;
            for (int y = 0; y < w->eye_h; y++)
                memset (w->composite_buf + y * cw3 + w->eye_w * 2 * 3,
                        0, pad);
        }
        /* Zero-fill padding rows if composite_h > eye_h. */
        for (int y = w->eye_h; y < w->composite_h; y++)
            memset (w->composite_buf + y * cw3, 0, cw3);

        ret = encoder_write_rgb (&w->enc[0], w->composite_buf,
                                 w->composite_w, fps);
        break;
    }
    case AG_VIDEO_LAYOUT_TOP_BOTTOM: {
        /* Copy left on top, right on bottom. */
        int ew3 = w->eye_w * 3;
        int cw3 = w->composite_w * 3;
        for (int y = 0; y < w->eye_h; y++) {
            memcpy (w->composite_buf + y * cw3,
                    left_rgb + y * ew3, ew3);
            /* Zero-fill horizontal padding if any. */
            if (w->composite_w > w->eye_w)
                memset (w->composite_buf + y * cw3 + ew3, 0,
                        (w->composite_w - w->eye_w) * 3);
        }
        for (int y = 0; y < w->eye_h; y++) {
            memcpy (w->composite_buf + (w->eye_h + y) * cw3,
                    right_rgb + y * ew3, ew3);
            if (w->composite_w > w->eye_w)
                memset (w->composite_buf + (w->eye_h + y) * cw3 + ew3, 0,
                        (w->composite_w - w->eye_w) * 3);
        }
        /* Zero-fill padding rows at bottom. */
        for (int y = w->eye_h * 2; y < w->composite_h; y++)
            memset (w->composite_buf + y * cw3, 0, cw3);

        ret = encoder_write_rgb (&w->enc[0], w->composite_buf,
                                 w->composite_w, fps);
        break;
    }
    case AG_VIDEO_LAYOUT_SEPARATE:
        ret  = encoder_write_rgb (&w->enc[0], left_rgb, w->composite_w, fps);
        ret |= encoder_write_rgb (&w->enc[1], right_rgb, w->composite_w, fps);
        break;
    case AG_VIDEO_LAYOUT_MONO:
        /* Stereo entry point on a mono writer is a programming error. */
        return -1;
    }

    if (ret == 0)
        w->frame_count++;

    return ret;
}

int
ag_video_writer_add_mono_frame (AgVideoWriter *w, const uint8_t *rgb)
{
    if (!w || !rgb) return -1;
    if (w->layout != AG_VIDEO_LAYOUT_MONO) return -1;

    int ret = encoder_write_rgb (&w->enc[0], rgb, w->composite_w, 1.0);
    if (ret == 0)
        w->frame_count++;
    return ret;
}

int64_t
ag_video_writer_close (AgVideoWriter *w)
{
    if (!w) return 0;

    int64_t count = w->frame_count;
    int flush_err = 0;

    for (int i = 0; i < w->n_encoders; i++) {
        if (encoder_flush (&w->enc[i]) < 0)
            flush_err = 1;
        encoder_free (&w->enc[i]);
    }

    free (w->composite_buf);
    free (w);

    if (flush_err) {
        fprintf (stderr, "warning: error flushing video encoder\n");
        return -1;
    }

    printf ("Recording finished (%lld frames written).\n",
            (long long) count);
    return count;
}
