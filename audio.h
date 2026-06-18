#pragma once
#include "player.h"

/*
 * Audio engine — wraps libmad (MP3), stb_vorbis (OGG), and raw WAV
 * decoding, then applies pitch shift and speed change via a
 * SoundTouch-style time-domain algorithm compiled for MIPS/VFPU.
 *
 * The PSP 2000's extra RAM lets us keep a larger decode buffer
 * (DECODE_BUF_SAMPLES) so we can do more processing per frame.
 */

// Output format — PSP hardware audio
#define SAMPLE_RATE     44100
#define CHANNELS        2        // stereo
#define AUDIO_BUF_SIZE  4096     // samples per hardware channel call

// Decode buffer — larger on Slim because we have the RAM
#define DECODE_BUF_SAMPLES (AUDIO_BUF_SIZE * 16)

// ── Lifecycle ────────────────────────────────────────────────────
void  audio_init        (void);
void  audio_shutdown    (void);

// ── Decoder ──────────────────────────────────────────────────────
void *audio_open_decoder(const char *path);
void  audio_close_decoder(void *decoder);
void  audio_get_metadata(void *decoder,
                          char *title,  int title_len,
                          char *artist, int artist_len,
                          char *album,  int album_len,
                          int  *duration_sec);

void  audio_play        (void *decoder);
void  audio_pause       (void *decoder);
void  audio_seek        (void *decoder, int second);
int   audio_is_finished (void *decoder);

// ── Per-frame update (decode + FX + queue) ───────────────────────
void  audio_update      (PlayerState *ps);
