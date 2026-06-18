#pragma once
#include <pspkernel.h>
#include <pspaudio.h>

#define MAX_PATH_LEN  256
#define MAX_TRACKS    512

// Speed range: 0.25x … 2.0x  (step 0.05)
#define SPEED_MIN   0.25f
#define SPEED_MAX   2.00f
#define SPEED_STEP  0.05f

// Pitch range: -12 … +12 semitones  (step 0.5)
#define PITCH_MIN  -12.0f
#define PITCH_MAX   12.0f
#define PITCH_STEP   0.5f

// Volume step
#define VOLUME_STEP  512

typedef enum {
    REPEAT_NONE = 0,
    REPEAT_ONE,
    REPEAT_ALL,
    REPEAT_COUNT
} RepeatMode;

typedef struct {
    // Track list
    char  tracks[MAX_TRACKS][MAX_PATH_LEN];
    int   track_count;
    int   current;

    // Playback state
    int   running;
    int   paused;
    int   shuffle;
    RepeatMode repeat;

    // Audio parameters
    int   volume;        // PSP audio volume (0 … PSP_AUDIO_VOLUME_MAX)
    float speed;         // Playback speed multiplier
    float pitch;         // Pitch shift in semitones

    // Decoder handle (opaque — managed by audio.c)
    void *decoder;

    // Track metadata
    char  title[128];
    char  artist[128];
    char  album[128];
    int   duration_sec;
    int   position_sec;
} PlayerState;

// ── API ──────────────────────────────────────────────────────────
void player_load          (PlayerState *ps, int index);
void player_toggle_pause  (PlayerState *ps);
void player_next          (PlayerState *ps);
void player_prev          (PlayerState *ps);
void player_check_end     (PlayerState *ps);

void player_volume_up     (PlayerState *ps);
void player_volume_down   (PlayerState *ps);
void player_speed_inc     (PlayerState *ps);
void player_speed_dec     (PlayerState *ps);
void player_pitch_inc     (PlayerState *ps);
void player_pitch_dec     (PlayerState *ps);
void player_reset_fx      (PlayerState *ps);
void player_toggle_shuffle(PlayerState *ps);
void player_cycle_repeat  (PlayerState *ps);
