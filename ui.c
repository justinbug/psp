/*
 * ui.c — 480×272 display using OSLib (OldSchoolLib)
 *
 * OSLib is the simplest 2D drawing library for PSP homebrew.
 * Install with: psp-pacman -S pspdev-oslib   (or grab from GitHub)
 *
 * Layout:
 *   ┌─────────────────────────────────────────┐  480×272
 *   │  ♪  PSP Music Player          [S] [R]   │  header bar
 *   ├─────────────────────────────────────────┤
 *   │  Title                                  │
 *   │  Artist — Album                         │
 *   │                                         │
 *   │  [████████████░░░░░░░░░░░░░░░]          │  progress
 *   │   0:42 / 3:18                           │
 *   │                                         │
 *   │  Speed: 1.00×   Pitch: +2.0 st          │
 *   │  Volume: ████████████░░░░               │
 *   ├─────────────────────────────────────────┤
 *   │  L/R:Speed  □/△:Pitch  ○:Reset  ×:Play  │  hint bar
 *   └─────────────────────────────────────────┘
 */

#include "ui.h"
#include <oslib/oslib.h>
#include <stdio.h>
#include <string.h>

// Colours (OSLib RGBA8888)
#define COL_BG       RGBA(15,  15,  20, 255)
#define COL_HEADER   RGBA(30,  30,  50, 255)
#define COL_ACCENT   RGBA(80, 180, 255, 255)
#define COL_TEXT     RGBA(220, 220, 220, 255)
#define COL_DIM      RGBA(120, 120, 130, 255)
#define COL_BAR_FG   RGBA(80, 180, 255, 255)
#define COL_BAR_BG   RGBA(40,  40,  60, 255)
#define COL_HINT     RGBA(70,  70,  90, 255)
#define COL_PAUSE    RGBA(255, 160,  60, 255)
#define COL_PLAY     RGBA( 80, 230,  80, 255)

void ui_init(void) {
    oslInit(0);
    oslInitGfx(PSP_DISPLAY_PIXEL_FORMAT_8888, 1);
    oslInitAudio();
    oslSetBilinear(1);
}

void ui_shutdown(void) {
    oslEndGfx();
    oslQuit();
}

// ── Helper: filled rectangle ──────────────────────────────────────
static void fill_rect(int x, int y, int w, int h, u32 col) {
    oslDrawFillRect(x, y, x+w, y+h, col);
}

// ── Helper: bar (e.g. progress, volume) ──────────────────────────
static void draw_bar(int x, int y, int w, int h,
                     float frac, u32 fg, u32 bg) {
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    fill_rect(x, y, w, h, bg);
    fill_rect(x, y, (int)(w * frac), h, fg);
}

// ── Helper: centered text ─────────────────────────────────────────
static void text_center(int cy, const char *s, u32 col) {
    int w = oslGetStringWidth(s);
    oslSetTextColor(col);
    oslDrawString(240 - w/2, cy, s);
}

static void text_left(int x, int y, const char *s, u32 col) {
    oslSetTextColor(col);
    oslDrawString(x, y, s);
}

// ── Truncate string to fit width ──────────────────────────────────
static void trunc_str(char *dst, const char *src, int max_w) {
    strncpy(dst, src, 255);
    dst[255] = '\0';
    while (*dst && oslGetStringWidth(dst) > max_w) {
        int l = strlen(dst);
        if (l < 4) break;
        dst[l-4] = '.'; dst[l-3] = '.'; dst[l-2] = '.'; dst[l-1] = '\0';
    }
}

// ── Format mm:ss ──────────────────────────────────────────────────
static void fmt_time(char *buf, int sec) {
    if (sec < 0) sec = 0;
    snprintf(buf, 16, "%d:%02d", sec/60, sec%60);
}

// ── Draw the full UI ──────────────────────────────────────────────
void ui_draw(PlayerState *ps) {
    oslStartDrawing();

    // Background
    fill_rect(0, 0, 480, 272, COL_BG);

    // ── Header bar ────────────────────────────────────────────────
    fill_rect(0, 0, 480, 28, COL_HEADER);
    text_left(10, 7, "PSP Music Player", COL_ACCENT);

    // Shuffle indicator
    char sh_str[16];
    snprintf(sh_str, sizeof(sh_str), "[%s]", ps->shuffle ? "SHF" : "---");
    text_left(370, 7, sh_str, ps->shuffle ? COL_ACCENT : COL_DIM);

    // Repeat indicator
    const char *rep_labels[] = {"---", "ONE", "ALL"};
    char rp_str[16];
    snprintf(rp_str, sizeof(rp_str), "[%s]", rep_labels[ps->repeat]);
    text_left(420, 7, rp_str, ps->repeat ? COL_ACCENT : COL_DIM);

    // ── Now playing ───────────────────────────────────────────────
    {
        char title_buf[256];
        char info_buf [256];

        if (ps->track_count == 0) {
            text_center(60, "No music found in ms0:/MUSIC/", COL_DIM);
            text_center(80, "Add .mp3, .ogg, or .wav files", COL_DIM);
        } else {
            trunc_str(title_buf, ps->title,  440);
            text_center(48, title_buf, COL_TEXT);

            if (ps->artist[0] && ps->album[0])
                snprintf(info_buf, sizeof(info_buf), "%s — %s",
                         ps->artist, ps->album);
            else if (ps->artist[0])
                snprintf(info_buf, sizeof(info_buf), "%s", ps->artist);
            else
                info_buf[0] = '\0';

            if (info_buf[0]) {
                trunc_str(info_buf, info_buf, 440);
                text_center(68, info_buf, COL_DIM);
            }

            // Track count
            char tc[32];
            snprintf(tc, sizeof(tc), "%d / %d",
                     ps->current + 1, ps->track_count);
            text_left(10, 48, tc, COL_DIM);
        }
    }

    // ── Progress bar ──────────────────────────────────────────────
    if (ps->track_count > 0 && ps->duration_sec > 0) {
        float frac = (float)ps->position_sec / (float)ps->duration_sec;
        draw_bar(20, 100, 440, 8, frac, COL_BAR_FG, COL_BAR_BG);

        char t_pos[16], t_dur[16], t_full[36];
        fmt_time(t_pos, ps->position_sec);
        fmt_time(t_dur, ps->duration_sec);
        snprintf(t_full, sizeof(t_full), "%s / %s", t_pos, t_dur);
        text_center(114, t_full, COL_DIM);
    }

    // ── Play/Pause status ─────────────────────────────────────────
    {
        const char *status = ps->paused ? "  ▮▮  PAUSED" : "  ▶  PLAYING";
        u32 sc = ps->paused ? COL_PAUSE : COL_PLAY;
        text_center(138, status, sc);
    }

    // ── Speed & pitch ─────────────────────────────────────────────
    {
        char sp[64];
        // Map pitch direction for display
        const char *pitch_dir = (ps->pitch > 0.05f)  ? "▲" :
                                (ps->pitch < -0.05f) ? "▼" : "—";
        snprintf(sp, sizeof(sp), "Speed: %.2fx     Pitch: %s%.1f st",
                 ps->speed, (ps->pitch > 0) ? "+" : "",
                 ps->pitch);
        text_center(168, sp, COL_TEXT);
    }

    // ── Volume bar ────────────────────────────────────────────────
    {
        float v = (float)ps->volume / (float)PSP_AUDIO_VOLUME_MAX;
        text_left(20, 190, "Vol", COL_DIM);
        draw_bar(50, 193, 360, 8, v, COL_BAR_FG, COL_BAR_BG);

        char pct[8];
        snprintf(pct, sizeof(pct), "%3d%%", (int)(v * 100));
        text_left(418, 190, pct, COL_DIM);
    }

    // ── Hint bar ──────────────────────────────────────────────────
    fill_rect(0, 248, 480, 24, COL_HINT);
    text_center(254,
        "L/R:Speed  \x81/\x80:Pitch  \x82:Reset  \x83:Play/Pause",
        COL_DIM);
    // PSP button glyphs: 0x80=△ 0x81=□ 0x82=○ 0x83=×
    // Actual rendering depends on your font; swap for text if needed.

    oslEndDrawing();
    oslSyncFrame();
}
