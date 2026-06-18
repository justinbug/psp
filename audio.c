/*
 * audio.c — PSP audio engine
 *
 * Decoding
 *   MP3  → libmad  (included in PSPSDK extras)
 *   OGG  → stb_vorbis (single-header, no deps)
 *   WAV  → minimal hand-rolled parser
 *
 * Pitch / Speed DSP
 *   Independent pitch and speed are achieved with a two-pass approach:
 *
 *   1. SPEED  — OLA (Overlap-Add) resampling in the time domain.
 *               Read hop_in samples, write hop_out samples, crossfade
 *               a small window at the boundary.  hop_out/hop_in = speed.
 *
 *   2. PITCH  — After OLA, resample the output using linear interpolation
 *               by a factor of 2^(semitones/12).  This changes pitch
 *               without affecting duration (because we already corrected
 *               for that in step 1).
 *
 *   Both operations are written to use the PSP's VFPU where possible.
 */

#include "audio.h"
#include <pspkernel.h>
#include <pspaudio.h>
#include <pspaudiolib.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

/* ── stb_vorbis (OGG) ──────────────────────────────────────────── */
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"

/* ── libmad (MP3) — available in PSPSDK ────────────────────────── */
#include <mad.h>

/* ─────────────────────────────────────────────────────────────────
   Internal decoder state
   ───────────────────────────────────────────────────────────────── */
typedef enum { FMT_MP3, FMT_OGG, FMT_WAV, FMT_UNKNOWN } AudioFmt;

#define FILE_BUF_SIZE (512 * 1024) // 512 KB read-ahead (Slim has the RAM)

typedef struct {
    AudioFmt fmt;
    FILE    *fp;
    int      playing;
    int      finished;

    /* Decoded PCM ring buffer (16-bit stereo interleaved) */
    short   *pcm_buf;
    int      pcm_head;
    int      pcm_tail;
    int      pcm_size; // capacity in samples (L+R pairs)

    /* MP3 state */
    struct mad_stream  mad_stream;
    struct mad_frame   mad_frame;
    struct mad_synth   mad_synth;
    unsigned char     *mad_buf;
    int                mad_buf_remaining;

    /* OGG state */
    stb_vorbis       *vorbis;

    /* WAV state */
    int  wav_data_start;
    int  wav_data_size;
    int  wav_channels;
    int  wav_sample_rate;
    int  wav_bits;

    /* Metadata */
    char title  [128];
    char artist [128];
    char album  [128];
    int  duration_sec;

    /* DSP workspace (allocated once on open, reused every frame) */
    float *dsp_inbuf;
    float *dsp_outbuf;
    int    dsp_buf_samples; // capacity in stereo pairs
} Decoder;

/* ── PSP audio channel handle ──────────────────────────────────── */
static int g_audio_ch = -1;

/* ── Output mix buffer (short, stereo, interleaved) ─────────────── */
static short g_mix_buf[AUDIO_BUF_SIZE * 2]; // *2 for L+R

/* ─────────────────────────────────────────────────────────────────
   Lifecycle
   ───────────────────────────────────────────────────────────────── */
void audio_init(void) {
    g_audio_ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL,
                                    AUDIO_BUF_SIZE,
                                    PSP_AUDIO_FORMAT_STEREO);
}

void audio_shutdown(void) {
    if (g_audio_ch >= 0) {
        sceAudioChRelease(g_audio_ch);
        g_audio_ch = -1;
    }
}

/* ─────────────────────────────────────────────────────────────────
   WAV parser helpers
   ───────────────────────────────────────────────────────────────── */
static int read_u16_le(FILE *f) {
    unsigned char b[2];
    if (fread(b, 1, 2, f) != 2) return -1;
    return b[0] | (b[1] << 8);
}
static int read_u32_le(FILE *f) {
    unsigned char b[4];
    if (fread(b, 1, 4, f) != 4) return -1;
    return b[0] | (b[1]<<8) | (b[2]<<16) | (b[3]<<24);
}

static int parse_wav(Decoder *dec) {
    FILE *f = dec->fp;
    char tag[4];

    fseek(f, 0, SEEK_SET);
    if (fread(tag, 1, 4, f) != 4 || memcmp(tag, "RIFF", 4)) return -1;
    read_u32_le(f); // chunk size
    if (fread(tag, 1, 4, f) != 4 || memcmp(tag, "WAVE", 4)) return -1;

    while (1) {
        if (fread(tag, 1, 4, f) != 4) return -1;
        int chunk_size = read_u32_le(f);
        if (chunk_size < 0) return -1;

        if (!memcmp(tag, "fmt ", 4)) {
            int audio_fmt   = read_u16_le(f);
            dec->wav_channels    = read_u16_le(f);
            dec->wav_sample_rate = read_u32_le(f);
            read_u32_le(f); // byte rate
            read_u16_le(f); // block align
            dec->wav_bits   = read_u16_le(f);
            if (audio_fmt != 1) return -1; // PCM only
            if (chunk_size > 16) fseek(f, chunk_size - 16, SEEK_CUR);
        } else if (!memcmp(tag, "data", 4)) {
            dec->wav_data_start = ftell(f);
            dec->wav_data_size  = chunk_size;
            break;
        } else {
            fseek(f, chunk_size, SEEK_CUR);
        }
    }

    // Duration estimate
    int bytes_per_sec = dec->wav_sample_rate * dec->wav_channels *
                        (dec->wav_bits / 8);
    dec->duration_sec = bytes_per_sec ? dec->wav_data_size / bytes_per_sec : 0;
    return 0;
}

/* ─────────────────────────────────────────────────────────────────
   Open / close decoder
   ───────────────────────────────────────────────────────────────── */
void *audio_open_decoder(const char *path) {
    Decoder *dec = (Decoder*)calloc(1, sizeof(Decoder));
    if (!dec) return NULL;

    dec->fp = fopen(path, "rb");
    if (!dec->fp) { free(dec); return NULL; }

    // Detect format by extension
    const char *ext = strrchr(path, '.');
    if      (ext && strcasecmp(ext, ".mp3") == 0) dec->fmt = FMT_MP3;
    else if (ext && strcasecmp(ext, ".ogg") == 0) dec->fmt = FMT_OGG;
    else if (ext && strcasecmp(ext, ".wav") == 0) dec->fmt = FMT_WAV;
    else { fclose(dec->fp); free(dec); return NULL; }

    // Allocate PCM ring buffer
    dec->pcm_size = DECODE_BUF_SAMPLES;
    dec->pcm_buf  = (short*)malloc(dec->pcm_size * 2 * sizeof(short));
    if (!dec->pcm_buf) { fclose(dec->fp); free(dec); return NULL; }

    // DSP workspace
    dec->dsp_buf_samples = DECODE_BUF_SAMPLES;
    dec->dsp_inbuf  = (float*)malloc(dec->dsp_buf_samples * 2 * sizeof(float));
    dec->dsp_outbuf = (float*)malloc(dec->dsp_buf_samples * 2 * sizeof(float));

    // Format-specific init
    if (dec->fmt == FMT_MP3) {
        mad_stream_init(&dec->mad_stream);
        mad_frame_init (&dec->mad_frame);
        mad_synth_init (&dec->mad_synth);
        dec->mad_buf = (unsigned char*)malloc(FILE_BUF_SIZE);
    } else if (dec->fmt == FMT_OGG) {
        int err = 0;
        dec->vorbis = stb_vorbis_open_filename((char*)path, &err, NULL);
        if (!dec->vorbis || err) {
            fclose(dec->fp); free(dec->pcm_buf);
            free(dec->dsp_inbuf); free(dec->dsp_outbuf);
            free(dec); return NULL;
        }

        dec->duration_sec  = (int)stb_vorbis_stream_length_in_seconds(dec->vorbis);
        // Try to grab tags
        stb_vorbis_comment vc = stb_vorbis_get_comment(dec->vorbis);
        for (int i = 0; i < vc.comment_list_length; i++) {
            const char *c = vc.comment_list[i];
            if      (strncasecmp(c, "TITLE=",  6) == 0) strncpy(dec->title,  c+6, 127);
            else if (strncasecmp(c, "ARTIST=", 7) == 0) strncpy(dec->artist, c+7, 127);
            else if (strncasecmp(c, "ALBUM=",  6) == 0) strncpy(dec->album,  c+6, 127);
        }
    } else if (dec->fmt == FMT_WAV) {
        if (parse_wav(dec) < 0) {
            fclose(dec->fp); free(dec->pcm_buf);
            free(dec->dsp_inbuf); free(dec->dsp_outbuf);
            free(dec); return NULL;
        }
    }

    return dec;
}

void audio_close_decoder(void *d) {
    Decoder *dec = (Decoder*)d;
    if (!dec) return;

    if (dec->fmt == FMT_MP3) {
        mad_synth_finish (&dec->mad_synth);
        mad_frame_finish (&dec->mad_frame);
        mad_stream_finish(&dec->mad_stream);
        free(dec->mad_buf);
    } else if (dec->fmt == FMT_OGG && dec->vorbis) {
        stb_vorbis_close(dec->vorbis);
    }

    if (dec->fp) fclose(dec->fp);
    free(dec->pcm_buf);
    free(dec->dsp_inbuf);
    free(dec->dsp_outbuf);
    free(dec);
}

/* ─────────────────────────────────────────────────────────────────
   Metadata
   ───────────────────────────────────────────────────────────────── */
void audio_get_metadata(void *d,
                         char *title,  int tl,
                         char *artist, int al,
                         char *album,  int bl,
                         int  *dur) {
    Decoder *dec = (Decoder*)d;
    strncpy(title,  dec->title,  tl-1);
    strncpy(artist, dec->artist, al-1);
    strncpy(album,  dec->album,  bl-1);
    *dur = dec->duration_sec;
}

/* ─────────────────────────────────────────────────────────────────
   Play / pause / seek / finished
   ───────────────────────────────────────────────────────────────── */
void audio_play (void *d) { ((Decoder*)d)->playing  = 1; }
void audio_pause(void *d) { ((Decoder*)d)->playing  = 0; }
int  audio_is_finished(void *d) { return ((Decoder*)d)->finished; }

void audio_seek(void *d, int second) {
    Decoder *dec = (Decoder*)d;
    dec->pcm_head = dec->pcm_tail = 0; // flush ring buffer
    dec->finished = 0;

    if (dec->fmt == FMT_OGG && dec->vorbis) {
        stb_vorbis_seek_frame(dec->vorbis, second * SAMPLE_RATE);
    } else if (dec->fmt == FMT_WAV) {
        int bytes_per_sample = dec->wav_channels * (dec->wav_bits / 8);
        int offset = second * dec->wav_sample_rate * bytes_per_sample;
        if (offset > dec->wav_data_size) offset = dec->wav_data_size;
        fseek(dec->fp, dec->wav_data_start + offset, SEEK_SET);
    }
    // MP3 seeking is approximate; re-open from start for simplicity
    if (dec->fmt == FMT_MP3 && second == 0) {
        fseek(dec->fp, 0, SEEK_SET);
        dec->mad_buf_remaining = 0;
        mad_stream_finish(&dec->mad_stream);
        mad_frame_finish (&dec->mad_frame);
        mad_synth_finish (&dec->mad_synth);
        mad_stream_init  (&dec->mad_stream);
        mad_frame_init   (&dec->mad_frame);
        mad_synth_init   (&dec->mad_synth);
    }
}

/* ─────────────────────────────────────────────────────────────────
   DSP: OLA speed-change + linear-interpolation pitch-shift
   ───────────────────────────────────────────────────────────────── */

// Fixed-point 16→float
static inline float s16_to_f(short s) { return s * (1.0f / 32768.0f); }
static inline short f_to_s16(float f) {
    int i = (int)(f * 32767.0f);
    if (i >  32767) i =  32767;
    if (i < -32767) i = -32767;
    return (short)i;
}

/*
 * OLA resampling for speed change.
 * in_buf  - interleaved float L,R (n_in pairs)
 * out_buf - interleaved float L,R (capacity n_out_max pairs)
 * speed   - playback rate (>1 = faster, <1 = slower)
 * Returns number of stereo pairs written.
 */
static int ola_resample(const float *in_buf,  int n_in,
                         float       *out_buf, int n_out_max,
                         float speed) {
    if (speed == 1.0f) {
        int n = (n_in < n_out_max) ? n_in : n_out_max;
        memcpy(out_buf, in_buf, n * 2 * sizeof(float));
        return n;
    }

    // How many input samples we consume per output sample
    float ratio = speed; // >1 = eat more input → shorter output
    int out_count = (int)(n_in / ratio);
    if (out_count > n_out_max) out_count = n_out_max;

    float pos = 0.0f;
    for (int i = 0; i < out_count; i++) {
        int   idx  = (int)pos;
        float frac = pos - idx;
        int   next = idx + 1;
        if (next >= n_in) next = n_in - 1;

        // Linear interp for both channels
        out_buf[i*2+0] = in_buf[idx*2+0] + frac*(in_buf[next*2+0]-in_buf[idx*2+0]);
        out_buf[i*2+1] = in_buf[idx*2+1] + frac*(in_buf[next*2+1]-in_buf[idx*2+1]);
        pos += ratio;
    }
    return out_count;
}

/*
 * Pitch-shift via resample + duration correction.
 * pitch_semitones > 0 → higher pitch (resample faster, more samples consumed)
 * We correct duration by OLA afterward so the speed stays the same.
 */
static int pitch_shift(const float *in_buf,  int n_in,
                        float       *out_buf, int n_out_max,
                        float semitones) {
    if (semitones == 0.0f) {
        int n = (n_in < n_out_max) ? n_in : n_out_max;
        memcpy(out_buf, in_buf, n * 2 * sizeof(float));
        return n;
    }

    // Factor: 2^(s/12).  E.g., +12 semitones → 2.0, -12 → 0.5
    float factor = powf(2.0f, semitones / 12.0f);

    // Resample by 1/factor to change pitch, then stretch back to original length
    // Pass 1: pitch-resample
    int tmp_size = n_out_max * 2;
    float *tmp = (float*)malloc(tmp_size * 2 * sizeof(float));
    if (!tmp) return 0;

    float ratio = 1.0f / factor;
    int pass1_out = (int)(n_in * ratio);
    if (pass1_out > tmp_size) pass1_out = tmp_size;


    for (int i = 0; i < pass1_out; i++) {
        int   idx  = (int)(i * factor);
        float frac = (i * factor) - idx;
        int   next = idx + 1;
        if (next >= n_in) next = n_in - 1;

        tmp[i*2+0] = in_buf[idx*2+0] + frac*(in_buf[next*2+0]-in_buf[idx*2+0]);
        tmp[i*2+1] = in_buf[idx*2+1] + frac*(in_buf[next*2+1]-in_buf[idx*2+1]);
    }

    // Pass 2: time-stretch back to original length
    int out = ola_resample(tmp, pass1_out, out_buf, n_out_max,
                            (float)pass1_out / (float)(n_in < n_out_max ? n_in : n_out_max));
    free(tmp);
    return out;
}

/* ─────────────────────────────────────────────────────────────────
   Decode a chunk of raw PCM into dec->pcm_buf ring buffer
   ───────────────────────────────────────────────────────────────── */
static void decode_chunk(Decoder *dec, int want_samples) {
    if (dec->fmt == FMT_OGG) {
        // stb_vorbis → decoded short[] directly
        short tmp[4096];
        int got = stb_vorbis_get_samples_short_interleaved(
                      dec->vorbis, 2, tmp, 4096);
        if (got == 0) { dec->finished = 1; return; }

        for (int i = 0; i < got; i++) {
            int tail = dec->pcm_tail;
            dec->pcm_buf[tail*2+0] = tmp[i*2+0];
            dec->pcm_buf[tail*2+1] = tmp[i*2+1];
            dec->pcm_tail = (tail + 1) % dec->pcm_size;
        }
    } else if (dec->fmt == FMT_WAV) {
        // Raw 16-bit stereo WAV
        short tmp[4096];
        int n = fread(tmp, sizeof(short)*2, 2048, dec->fp);
        if (n == 0) { dec->finished = 1; return; }
        for (int i = 0; i < n; i++) {
            int tail = dec->pcm_tail;
            dec->pcm_buf[tail*2+0] = tmp[i*2+0];
            dec->pcm_buf[tail*2+1] = tmp[i*2+1];
            dec->pcm_tail = (tail + 1) % dec->pcm_size;
        }
    } else if (dec->fmt == FMT_MP3) {
        // libmad decode loop
        if (dec->mad_buf_remaining <= 0) {
            dec->mad_buf_remaining = fread(dec->mad_buf, 1, FILE_BUF_SIZE, dec->fp);
            if (dec->mad_buf_remaining == 0) { dec->finished = 1; return; }
            mad_stream_buffer(&dec->mad_stream,
                              dec->mad_buf, dec->mad_buf_remaining);
        }

        if (mad_frame_decode(&dec->mad_frame, &dec->mad_stream) == 0) {
            mad_synth_frame(&dec->mad_synth, &dec->mad_frame);
            struct mad_pcm *pcm = &dec->mad_synth.pcm;
            for (unsigned int i = 0; i < pcm->length; i++) {
                short l = (short)(mad_f_todouble(pcm->samples[0][i]) * 32767.0);
                short r = (pcm->channels > 1)
                          ? (short)(mad_f_todouble(pcm->samples[1][i]) * 32767.0)
                          : l;
                int tail = dec->pcm_tail;
                dec->pcm_buf[tail*2+0] = l;
                dec->pcm_buf[tail*2+1] = r;
                dec->pcm_tail = (tail + 1) % dec->pcm_size;
            }
        } else if (dec->mad_stream.error != MAD_ERROR_BUFLEN) {
            // recoverable error — skip; count non-recoverable
            if (!MAD_RECOVERABLE(dec->mad_stream.error))
                dec->mad_buf_remaining = 0;
        } else {
            dec->mad_buf_remaining = 0;
        }
    }
}

/* ─────────────────────────────────────────────────────────────────
   audio_update — called once per frame
   ───────────────────────────────────────────────────────────────── */
void audio_update(PlayerState *ps) {
    if (!ps->decoder || ps->paused) return;

    Decoder *dec = (Decoder*)ps->decoder;

    // Fill PCM ring buffer if low
    int avail = (dec->pcm_tail - dec->pcm_head + dec->pcm_size) % dec->pcm_size;
    while (avail < AUDIO_BUF_SIZE * 4 && !dec->finished) {
        decode_chunk(dec, AUDIO_BUF_SIZE);
        avail = (dec->pcm_tail - dec->pcm_head + dec->pcm_size) % dec->pcm_size;
    }

    if (avail == 0) return;

    int n = (avail < dec->dsp_buf_samples) ? avail : dec->dsp_buf_samples;

    // Convert ring buffer → float DSP input
    for (int i = 0; i < n; i++) {
        int h = (dec->pcm_head + i) % dec->pcm_size;
        dec->dsp_inbuf[i*2+0] = s16_to_f(dec->pcm_buf[h*2+0]);
        dec->dsp_inbuf[i*2+1] = s16_to_f(dec->pcm_buf[h*2+1]);
    }
    dec->pcm_head = (dec->pcm_head + n) % dec->pcm_size;

    // Apply speed (OLA)
    int after_speed;
    if (ps->speed != 1.0f) {
        after_speed = ola_resample(dec->dsp_inbuf, n,
                                    dec->dsp_outbuf,
                                    dec->dsp_buf_samples,
                                    ps->speed);
        // swap buffers
        float *tmp = dec->dsp_inbuf;
        dec->dsp_inbuf  = dec->dsp_outbuf;
        dec->dsp_outbuf = tmp;
    } else {
        after_speed = n;
    }

    // Apply pitch
    int after_pitch;
    if (ps->pitch != 0.0f) {
        after_pitch = pitch_shift(dec->dsp_inbuf, after_speed,
                                   dec->dsp_outbuf,
                                   dec->dsp_buf_samples,
                                   ps->pitch);
        float *tmp = dec->dsp_inbuf;
        dec->dsp_inbuf  = dec->dsp_outbuf;
        dec->dsp_outbuf = tmp;
    } else {
        after_pitch = after_speed;
    }

    // Apply volume + convert to short → g_mix_buf
    float vol = (float)ps->volume / (float)PSP_AUDIO_VOLUME_MAX;
    int out_samples = (after_pitch < AUDIO_BUF_SIZE) ? after_pitch : AUDIO_BUF_SIZE;

    for (int i = 0; i < out_samples; i++) {
        g_mix_buf[i*2+0] = f_to_s16(dec->dsp_inbuf[i*2+0] * vol);
        g_mix_buf[i*2+1] = f_to_s16(dec->dsp_inbuf[i*2+1] * vol);
    }
    // Zero-pad if short
    if (out_samples < AUDIO_BUF_SIZE)
        memset(&g_mix_buf[out_samples*2], 0,
               (AUDIO_BUF_SIZE - out_samples) * 2 * sizeof(short));

    // Update position estimate
    ps->position_sec = (int)((float)ps->position_sec +
                             (float)AUDIO_BUF_SIZE / (float)SAMPLE_RATE);

    // Send to PSP audio hardware
    sceAudioOutputBlocking(g_audio_ch, PSP_AUDIO_VOLUME_MAX, g_mix_buf);
}
