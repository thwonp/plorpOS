/* SPDX-License-Identifier: MIT */
/* See dec.h. */
#include "dec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>

/* The resampler, and the only part of this chain anyone would hear go wrong.
 *
 * The Brick's dmix runs at exactly 48000 Hz and nearly every music file and
 * podcast is 44100, so every one of them is resampled SOMEWHERE on the way to
 * the speaker. Left to ALSA it is `plug` with Allwinner's awrate plugin, of
 * unknown quality; here it is a 64-tap sinc filter with triangular dither
 * before the drop to 16 bits. Measured on the device: half a percent of a core
 * more than FFmpeg's 32-tap default, which is the cheapest audible gain there
 * is. atempo, when the speed is not 1, goes in front of it.
 *
 * And 2 dB of headroom ahead of the resampler, in floating point. Loud masters
 * decode past full scale, and the drop to 16 bits cut those peaks off flat:
 * heard as a crackle on the GKD Pixel 2 at the same moments of a song every
 * time, found 2026-10-02 in a recording of what Muse handed the mixer. Through
 * this chain, 55 of the 69 tracks on the test card peaked over full scale, the
 * worst at +1.54 dB (Raphael Saadiq, "Let's Take A Walk"). 2 dB clears them
 * all, and costs 2 dB at the top of the volume scale and nothing else - a
 * limiter would keep the loudness, for CPU, latency and a changed sound on
 * exactly the loud songs this is about. */
#define GRAPH_TAIL \
	"volume=-2dB:precision=float," \
	"aresample=48000:filter_size=64:cutoff=0.97:dither_method=triangular," \
	"aformat=sample_fmts=s16:channel_layouts=stereo"

struct dec {
	AVFormatContext *fmt;
	AVCodecContext  *cc;
	int              si;
	AVFilterGraph   *g;
	AVFilterContext *src, *sink;
	AVPacket        *pkt;
	AVFrame         *in, *out;
	int              out_off;    /* frames of `out` already handed over */
	int              out_have;
	int              fed_eof;    /* the graph has been told there is no more */
	double           speed;
	double           at;         /* frames ending before this are dropped */
	double           base;       /* file seconds at the first frame kept */
	int              base_set;
	int64_t          given;      /* frames handed over since `base` */
	AVAudioFifo     *held;       /* the last chain's tail, after a speed change */
	int              cut;        /* the end came from a read that failed */
};

static int build_graph(dec *d, char *err, size_t errn)
{
	char args[512], layout[64], chain[256];
	AVFilterInOut *ins = NULL, *outs = NULL;
	int ok = 0;

	d->g = avfilter_graph_alloc();
	if (!d->g) { snprintf(err, errn, "no memory for a filter graph"); return 0; }
	av_channel_layout_describe(&d->cc->ch_layout, layout, sizeof layout);
	snprintf(args, sizeof args,
	         "time_base=1/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
	         d->cc->sample_rate, d->cc->sample_rate,
	         av_get_sample_fmt_name(d->cc->sample_fmt), layout);
	if (d->speed > 1.001 || d->speed < 0.999)
		snprintf(chain, sizeof chain, "atempo=%.3f," GRAPH_TAIL, d->speed);
	else
		snprintf(chain, sizeof chain, "%s", GRAPH_TAIL);

	outs = avfilter_inout_alloc();
	ins  = avfilter_inout_alloc();
	if (!outs || !ins) goto done;
	if (avfilter_graph_create_filter(&d->src, avfilter_get_by_name("abuffer"),
	                                 "in", args, NULL, d->g) < 0 ||
	    avfilter_graph_create_filter(&d->sink, avfilter_get_by_name("abuffersink"),
	                                 "out", NULL, NULL, d->g) < 0)
		goto done;
	outs->name = av_strdup("in");  outs->filter_ctx = d->src;  outs->pad_idx = 0;
	ins->name  = av_strdup("out"); ins->filter_ctx  = d->sink; ins->pad_idx  = 0;
	if (avfilter_graph_parse_ptr(d->g, chain, &ins, &outs, NULL) < 0 ||
	    avfilter_graph_config(d->g, NULL) < 0)
		goto done;
	ok = 1;
done:
	avfilter_inout_free(&ins);
	avfilter_inout_free(&outs);
	if (!ok) snprintf(err, errn, "cannot build the audio chain");
	return ok;
}

dec *dec_open(const char *path, const char *proxy, double at, double speed, char *err, size_t errn)
{
	dec *d = calloc(1, sizeof *d);
	const AVCodec *codec = NULL;

	/* FFmpeg's own log goes to stderr, which is the launcher's log on the
	 * card, and some m4a files draw a warning or two per file - SD wear for
	 * nothing (gkd.33). What goes wrong is reported through err instead. */
	av_log_set_level(AV_LOG_QUIET);
	if (!d) { snprintf(err, errn, "no memory"); return NULL; }
	d->speed = speed > 0.1 ? speed : 1.0;
	d->at = at > 0 ? at : 0;

	{
		/* A stream gives up after 10 s of silence from the server, rather
		 * than waiting forever on a connection Wi-Fi took away mid-song: the
		 * read fails, the track ends, and the launcher finds the server
		 * again for the next (plorpos-1lw.19). Files are not affected. */
		AVDictionary *o = NULL;
		char *to = NULL;
		int e;

		/* Where an https stream's redirects end, opened rather than the URL
		 * given: FFmpeg's seek goes back to the URL it was given and walks
		 * every redirect again, since a podcast's tracking hops (302s, no
		 * Expires) are never cached - 4 hops, 1.6-1.9 s a seek on the 34xxsp
		 * against 0.3 s to the end of them (plorpos-7ni.10). One connection
		 * more on opening. Plain http (Plex, Audiobookshelf on the LAN) is
		 * opened once, as it was. */
		if (!strncmp(path, "https://", 8)) {
			AVIOContext *pb = NULL;
			uint8_t *loc = NULL;

			av_dict_set(&o, "rw_timeout", "10000000", 0);
			if (proxy && proxy[0]) av_dict_set(&o, "http_proxy", proxy, 0);
			if (avio_open2(&pb, path, AVIO_FLAG_READ, NULL, &o) >= 0) {
				if (av_opt_get(pb, "location", AV_OPT_SEARCH_CHILDREN, &loc) >= 0 && loc &&
				    strcmp((char *)loc, path) && strstr((char *)loc, "://"))
					to = strdup((char *)loc);
				av_free(loc);
				avio_closep(&pb);
			}
			av_dict_free(&o);
		}
		if (strstr(path, "://")) {
			av_dict_set(&o, "rw_timeout", "10000000", 0);
			if (proxy && proxy[0]) av_dict_set(&o, "http_proxy", proxy, 0);
		}
		e = avformat_open_input(&d->fmt, to ? to : path, NULL, &o);
		if (e < 0 && to) {                                         /* as given, then */
			/* The failed open used up the options: set them again, or the
			 * retry would leave the proxy out. */
			av_dict_free(&o);
			av_dict_set(&o, "rw_timeout", "10000000", 0);
			if (proxy && proxy[0]) av_dict_set(&o, "http_proxy", proxy, 0);
			e = avformat_open_input(&d->fmt, path, NULL, &o);
		}
		free(to);
		av_dict_free(&o);
		/* An https stream's MP3 seeks to the byte its time is at, rather than
		 * reading forward from the last frame it indexed - the whole way, over
		 * Wi-Fi, which was most of a seek's 2-7 s (plorpos-7ni.10). Exact for
		 * CBR, which a podcast's MP3 nearly always is; a VBR one lands near.
		 * Files on the card, and http on the LAN, keep the exact seek. */
		if (e >= 0 && !strncmp(path, "https://", 8)) d->fmt->flags |= AVFMT_FLAG_FAST_SEEK;

		if (e < 0) {
			char why[64];

			av_strerror(e, why, sizeof why);
			snprintf(err, errn, "cannot open the file (%s)", why);
			goto fail;
		}
	}
	if (avformat_find_stream_info(d->fmt, NULL) < 0) {
		snprintf(err, errn, "cannot read the file's streams");
		goto fail;
	}
	d->si = av_find_best_stream(d->fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
	if (d->si < 0 || !codec) { snprintf(err, errn, "no audio in the file"); goto fail; }

	d->cc = avcodec_alloc_context3(codec);
	if (!d->cc ||
	    avcodec_parameters_to_context(d->cc, d->fmt->streams[d->si]->codecpar) < 0 ||
	    avcodec_open2(d->cc, codec, NULL) < 0) {
		snprintf(err, errn, "cannot start the %s decoder", codec->name);
		goto fail;
	}
	if (!build_graph(d, err, errn)) goto fail;

	d->pkt = av_packet_alloc();
	d->in  = av_frame_alloc();
	d->out = av_frame_alloc();
	if (!d->pkt || !d->in || !d->out) { snprintf(err, errn, "no memory"); goto fail; }

	/* A seek lands on a packet boundary at or before the target, so what
	 * dec_read drops is whole frames that end before it. At most one frame
	 * early - 26 ms of MP3, 23 of AAC - which is inaudible on a resume and a
	 * great deal simpler than trimming inside a planar frame. */
	if (d->at > 0)
		av_seek_frame(d->fmt, -1, (int64_t)(d->at * AV_TIME_BASE),
		              AVSEEK_FLAG_BACKWARD);
	return d;
fail:
	dec_close(d);
	return NULL;
}

/* Push one decoded frame into the graph, or the end of the file. Returns 1 if
 * the graph was fed, 0 at the end, <0 on an error. */
static int feed(dec *d)
{
	AVStream *st = d->fmt->streams[d->si];

	for (;;) {
		int r = avcodec_receive_frame(d->cc, d->in);

		if (r == 0) {
			double t = d->in->best_effort_timestamp == AV_NOPTS_VALUE ? 0 :
			           d->in->best_effort_timestamp * av_q2d(st->time_base);
			double dur = (double)d->in->nb_samples / d->cc->sample_rate;

			if (t + dur <= d->at) { av_frame_unref(d->in); continue; }
			if (!d->base_set) { d->base = t; d->base_set = 1; }
			r = av_buffersrc_add_frame(d->src, d->in);
			av_frame_unref(d->in);
			return r < 0 ? r : 1;
		}
		if (r == AVERROR_EOF) {
			if (!d->fed_eof) {
				d->fed_eof = 1;
				return av_buffersrc_add_frame(d->src, NULL) < 0 ? -1 : 1;
			}
			return 0;
		}
		if (r != AVERROR(EAGAIN)) return r;

		/* The decoder wants a packet. */
		r = av_read_frame(d->fmt, d->pkt);
		if (r < 0) {
			if (r != AVERROR_EOF) d->cut = 1;
			avcodec_send_packet(d->cc, NULL);      /* flush, then drain above */
			continue;
		}
		if (d->pkt->stream_index == d->si)
			avcodec_send_packet(d->cc, d->pkt);   /* a bad packet is skipped */
		av_packet_unref(d->pkt);
	}
}

int dec_read(dec *d, int16_t *buf, int max)
{
	int got = 0;

	while (got < max) {
		if (d->out_have) {
			int left = d->out->nb_samples - d->out_off;
			int take = left < max - got ? left : max - got;

			memcpy(buf + (size_t)got * DEC_CHANNELS,
			       (int16_t *)d->out->data[0] + (size_t)d->out_off * DEC_CHANNELS,
			       (size_t)take * DEC_CHANNELS * sizeof *buf);
			got += take;
			d->out_off += take;
			if (d->out_off >= d->out->nb_samples) {
				av_frame_unref(d->out);
				d->out_have = 0;
			}
			continue;
		}
		if (d->held && av_audio_fifo_size(d->held) > 0) {
			void *p = buf + (size_t)got * DEC_CHANNELS;

			got += av_audio_fifo_read(d->held, &p, max - got);
			continue;
		}
		{
			int r = av_buffersink_get_frame(d->sink, d->out);

			if (r >= 0) { d->out_have = 1; d->out_off = 0; continue; }
			if (r == AVERROR_EOF) break;
			if (r != AVERROR(EAGAIN)) return got ? got : -1;
		}
		{
			int r = feed(d);

			if (r < 0) return got ? got : -1;
			/* 0 means the graph already has its end; the next get_frame
			 * returns the tail and then EOF. */
		}
	}
	d->given += got;
	return got;
}

double dec_pos(const dec *d)
{
	return (d->base_set ? d->base : d->at) +
	       (double)d->given * d->speed / DEC_RATE;
}

double dec_len(const dec *d)
{
	return d->fmt && d->fmt->duration != AV_NOPTS_VALUE
	       ? d->fmt->duration / (double)AV_TIME_BASE : 0;
}

void dec_codec(const dec *d, char *out, size_t n)
{
	static const int STD[] = { 32, 48, 64, 96, 112, 128, 160, 192, 224, 256, 320 };
	const AVCodecParameters *p;
	const char *name, *fmt;
	char up[16];
	bool lossless = false;
	int64_t br;
	int i, kbps, bits;

	*out = '\0';
	if (!d || !d->fmt || d->si < 0) return;
	p = d->fmt->streams[d->si]->codecpar;
	fmt = d->fmt->iformat ? d->fmt->iformat->name : "";
	switch (p->codec_id) {
	case AV_CODEC_ID_MP3:     name = "MP3"; break;
	case AV_CODEC_ID_AAC:     name = "AAC"; break;
	case AV_CODEC_ID_OPUS:    name = "OPUS"; break;
	case AV_CODEC_ID_VORBIS:  name = "VORBIS"; break;
	case AV_CODEC_ID_FLAC:    name = "FLAC"; lossless = true; break;
	case AV_CODEC_ID_ALAC:    name = "ALAC"; lossless = true; break;
	case AV_CODEC_ID_WAVPACK: name = "WV"; lossless = true; break;
	case AV_CODEC_ID_APE:     name = "APE"; lossless = true; break;
	default:
		if (p->codec_id >= AV_CODEC_ID_FIRST_AUDIO && p->codec_id < AV_CODEC_ID_ADPCM_IMA_QT) {
			name = strstr(fmt, "wav") ? "WAV" : strstr(fmt, "aiff") ? "AIFF" : "PCM";
			lossless = true;
		} else {
			snprintf(up, sizeof up, "%s", avcodec_get_name(p->codec_id));
			for (i = 0; up[i]; i++) if (up[i] >= 'a' && up[i] <= 'z') up[i] -= 32;
			name = up;
		}
	}
	if (lossless) {
		char khz[16];

		bits = p->bits_per_raw_sample ? p->bits_per_raw_sample : p->bits_per_coded_sample;
		snprintf(khz, sizeof khz, "%.1f", p->sample_rate / 1000.0);
		if (strlen(khz) > 2 && !strcmp(khz + strlen(khz) - 2, ".0")) khz[strlen(khz) - 2] = '\0';
		if (!p->sample_rate) snprintf(out, n, "%s", name);
		else if (bits) snprintf(out, n, "%s %d/%s", name, bits, khz);
		else snprintf(out, n, "%s %s", name, khz);
		return;
	}
	br = p->bit_rate ? p->bit_rate : d->fmt->bit_rate;
	kbps = (int)((br + 500) / 1000);
	/* The nearest standard rate within 8% (a tie goes up): VBR averages
	 * wander - a 128 kbps Opus from Plex reads 120 from its size. */
	{
		int best = 0;

		for (i = 0; i < (int)(sizeof STD / sizeof STD[0]); i++)
			if (!best || abs(kbps - STD[i]) <= abs(kbps - best)) best = STD[i];
		if (kbps * 100 >= best * 92 && kbps * 100 <= best * 108) kbps = best;
	}
	if (kbps > 0) snprintf(out, n, "%s %d", name, kbps);
	else snprintf(out, n, "%s", name);
}

const char *dec_tag(const dec *d, const char *key)
{
	AVDictionaryEntry *e = NULL;

	/* Container tags first, then the stream's: Ogg keeps Vorbis and Opus
	 * comments on the STREAM, so a container-only lookup reads every .opus
	 * and .ogg as untitled. */
	if (d->fmt) e = av_dict_get(d->fmt->metadata, key, NULL, 0);
	if (!e && d->fmt && d->si >= 0)
		e = av_dict_get(d->fmt->streams[d->si]->metadata, key, NULL, 0);
	return e ? e->value : "";
}

int dec_chapters(const dec *d)
{
	return d->fmt ? (int)d->fmt->nb_chapters : 0;
}

double dec_chapter_at(const dec *d, int i)
{
	AVChapter *c = d->fmt->chapters[i];

	return c->start * av_q2d(c->time_base);
}

const char *dec_chapter_title(const dec *d, int i)
{
	AVDictionaryEntry *e = av_dict_get(d->fmt->chapters[i]->metadata, "title",
	                                   NULL, 0);
	return e ? e->value : "";
}

int dec_seek(dec *d, double at)
{
	char err[64];

	if (!d || !d->fmt) return 0;
	if (at < 0) at = 0;
	if (av_seek_frame(d->fmt, -1, (int64_t)(at * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD) < 0)
		return 0;
	avcodec_flush_buffers(d->cc);
	av_frame_unref(d->in);
	av_frame_unref(d->out);
	d->out_have = d->out_off = 0;
	if (d->held) av_audio_fifo_reset(d->held);
	avfilter_graph_free(&d->g);
	if (!build_graph(d, err, sizeof err)) return 0;
	d->fed_eof = 0;
	d->cut = 0;
	d->at = at;
	d->base_set = 0;
	d->given = 0;
	return 1;
}

int dec_cut(const dec *d)
{
	return d && d->cut;
}

int dec_speed(dec *d, double speed)
{
	char err[64];
	double was = d ? d->speed : 1;
	int64_t left;

	if (!d || !d->g || speed < 0.1) return 0;
	if (!d->held && !(d->held = av_audio_fifo_alloc(AV_SAMPLE_FMT_S16, DEC_CHANNELS, 8192)))
		return 0;
	/* The old chain told it has everything, so atempo hands over what it was
	 * still holding; that plays, at the old speed, before the new chain's. */
	if (!d->fed_eof && av_buffersrc_add_frame(d->src, NULL) < 0) return 0;
	while (av_buffersink_get_frame(d->sink, d->in) >= 0) {
		av_audio_fifo_write(d->held, (void **)d->in->data, d->in->nb_samples);
		av_frame_unref(d->in);
	}
	avfilter_graph_free(&d->g);
	d->speed = speed;
	if (!build_graph(d, err, sizeof err)) return 0;
	if (d->fed_eof && av_buffersrc_add_frame(d->src, NULL) < 0) return 0;   /* nothing more comes */
	/* dec_pos counts frames at the speed they were made at: the ones not yet
	 * handed over were made at the old one. */
	left = av_audio_fifo_size(d->held) + (d->out_have ? d->out->nb_samples - d->out_off : 0);
	if (d->base_set) {
		d->base += ((double)(d->given + left) * was - (double)left * speed) / DEC_RATE;
		d->given = 0;
	}
	return 1;
}

void dec_close(dec *d)
{
	if (!d) return;
	av_frame_free(&d->in);
	av_frame_free(&d->out);
	av_packet_free(&d->pkt);
	avfilter_graph_free(&d->g);
	if (d->held) av_audio_fifo_free(d->held);
	avcodec_free_context(&d->cc);
	avformat_close_input(&d->fmt);
	free(d);
}
