#include "player.h"
#include "audio.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ── Load a track by index ─────────────────────────────────────────
void player_load(PlayerState *ps, int index) {
    if (ps->track_count == 0) return;

    // Clamp
    if (index < 0) index = ps->track_count - 1;
    if (index >= ps->track_count) index = 0;

    ps->current      = index;
    ps->position_sec = 0;

    // Stop previous decoder
    if (ps->decoder) {
        audio_close_decoder(ps->decoder);
        ps->decoder = NULL;
    }

    // Open new decoder
    ps->decoder = audio_open_decoder(ps->tracks[index]);
    if (!ps->decoder) {
        snprintf(ps->title, sizeof(ps->title), "Error loading file");
        ps->artist[0] = ps->album[0] = '\0';
        ps->duration_sec = 0;
        return;
    }

    // Pull metadata
    audio_get_metadata(ps->decoder,
                       ps->title,  sizeof(ps->title),
                       ps->artist, sizeof(ps->artist),
                       ps->album,  sizeof(ps->album),
                       &ps->duration_sec);

    // If title is blank, use filename
    if (ps->title[0] == '\0') {
        const char *slash = strrchr(ps->tracks[index], '/');
        strncpy(ps->title, slash ? slash + 1 : ps->tracks[index],
                sizeof(ps->title) - 1);
    }

    if (!ps->paused)
        audio_play(ps->decoder);
}

// ── Pause / resume ────────────────────────────────────────────────
void player_toggle_pause(PlayerState *ps) {
    ps->paused = !ps->paused;
    if (ps->decoder) {
        if (ps->paused) audio_pause(ps->decoder);
        else            audio_play (ps->decoder);
    }
}

// ── Navigation ────────────────────────────────────────────────────
void player_next(PlayerState *ps) {
    int next;
    if (ps->shuffle)
        next = rand() % ps->track_count;
    else
        next = (ps->current + 1) % ps->track_count;
    player_load(ps, next);
}

void player_prev(PlayerState *ps) {
    // If >3 s in, restart; otherwise go back
    if (ps->position_sec > 3) {
        audio_seek(ps->decoder, 0);
        ps->position_sec = 0;
    } else {
        int prev = (ps->current - 1 + ps->track_count) % ps->track_count;
        player_load(ps, prev);
    }
}

// ── Auto-advance when track ends ──────────────────────────────────
void player_check_end(PlayerState *ps) {
    if (!ps->decoder) return;
    if (!audio_is_finished(ps->decoder)) return;

    switch (ps->repeat) {
        case REPEAT_ONE:
            audio_seek(ps->decoder, 0);
            audio_play(ps->decoder);
            ps->position_sec = 0;
            break;
        case REPEAT_ALL:
            player_next(ps);
            break;
        case REPEAT_NONE:
        default:
            if (ps->current < ps->track_count - 1)
                player_next(ps);
            else
                ps->paused = 1;
            break;
    }
}

// ── Volume ────────────────────────────────────────────────────────
void player_volume_up(PlayerState *ps) {
    ps->volume += VOLUME_STEP;
    if (ps->volume > PSP_AUDIO_VOLUME_MAX)
        ps->volume = PSP_AUDIO_VOLUME_MAX;
}

void player_volume_down(PlayerState *ps) {
    ps->volume -= VOLUME_STEP;
    if (ps->volume < 0) ps->volume = 0;
}

// ── Speed ─────────────────────────────────────────────────────────
void player_speed_inc(PlayerState *ps) {
    ps->speed += SPEED_STEP;
    if (ps->speed > SPEED_MAX) ps->speed = SPEED_MAX;
}

void player_speed_dec(PlayerState *ps) {
    ps->speed -= SPEED_STEP;
    if (ps->speed < SPEED_MIN) ps->speed = SPEED_MIN;
}

// ── Pitch ─────────────────────────────────────────────────────────
void player_pitch_inc(PlayerState *ps) {
    ps->pitch += PITCH_STEP;
    if (ps->pitch > PITCH_MAX) ps->pitch = PITCH_MAX;
}

void player_pitch_dec(PlayerState *ps) {
    ps->pitch -= PITCH_STEP;
    if (ps->pitch < PITCH_MIN) ps->pitch = PITCH_MIN;
}

// ── Reset effects ─────────────────────────────────────────────────
void player_reset_fx(PlayerState *ps) {
    ps->speed = 1.0f;
    ps->pitch = 0.0f;
}

// ── Shuffle / Repeat ─────────────────────────────────────────────
void player_toggle_shuffle(PlayerState *ps) {
    ps->shuffle = !ps->shuffle;
}

void player_cycle_repeat(PlayerState *ps) {
    ps->repeat = (ps->repeat + 1) % REPEAT_COUNT;
}
