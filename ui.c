/*
 * ui.c — PSP display using raw pspgu + psplib intraFont
 * No OSLib required — everything is in the base PSPSDK.
 *
 * Font: intraFont (PSP's own pgf font files from flash0)
 *   flash0:/font/ltn0.pgf  — Latin regular
 * intraFont is bundled with pspdev: https://github.com/pspdev/intraFont
 */

#include "ui.h"
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <intraFont.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define SCREEN_W  480
#define SCREEN_H  272
#define BUF_W     512   // GU requires power-of-two width

/* ── Colours RGBA8888 ─────────────────────────────────────────── */
#define COL_BG      0xFF140F0F
#define COL_HEADER  0xFF321E1E
#define COL_ACCENT  0xFFFFB450
#define COL_TEXT    0xFFDCDCDC
#define COL_DIM     0xFF888888
#define COL_BAR_FG  0xFFFFB450
#define COL_BAR_BG  0xFF2A2A3A
#define COL_HINT    0xFF1E1E2E
#define COL_PLAY    0xFF50E650
#define COL_PAUSE   0xFFFFA030

/* ── GU display lists & framebuffers ─────────────────────────── */
static unsigned int __attribute__((aligned(16))) g_list[262144];
static void *g_fbp0;
static void *g_fbp1;
static void *g_zbp;

static intraFont *g_font      = NULL;
static intraFont *g_font_small = NULL;

/* ── Vertex type for filled rects ───────────────────────────── */
typedef struct {
    short x, y, z;
} Vert;

/* ── Init ────────────────────────────────────────────────────── */
void ui_init(void) {
    g_fbp0 = (void*)0;
    g_fbp1 = (void*)(BUF_W * SCREEN_H * 4);
    g_zbp  = (void*)(BUF_W * SCREEN_H * 4 * 2);

    sceGuInit();
    sceGuStart(GU_DIRECT, g_list);

    sceGuDrawBuffer (GU_PSM_8888, g_fbp0, BUF_W);
    sceGuDispBuffer (SCREEN_W, SCREEN_H, g_fbp1, BUF_W);
    sceGuDepthBuffer(g_zbp, BUF_W);

    sceGuOffset(2048 - SCREEN_W/2, 2048 - SCREEN_H/2);
    sceGuViewport(2048, 2048, SCREEN_W, SCREEN_H);
    sceGuDepthRange(0xc350, 0x2710);

    sceGuScissor(0, 0, SCREEN_W, SCREEN_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuShadeModel(GU_FLAT);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_LIGHTING);

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    intraFontInit();
    g_font       = intraFontLoad("flash0:/font/ltn0.pgf", INTRAFONT_CACHE_ALL);
    g_font_small = intraFontLoad("flash0:/font/ltn0.pgf", INTRAFONT_CACHE_ALL);
    if (g_font)       intraFontSetStyle(g_font,       1.0f, 0xFFFFFFFF, 0, 0, INTRAFONT_ALIGN_LEFT);
    if (g_font_small) intraFontSetStyle(g_font_small, 0.75f,0xFFFFFFFF, 0, 0, INTRAFONT_ALIGN_LEFT);
}

void ui_shutdown(void) {
    if (g_font)       intraFontUnload(g_font);
    if (g_font_small) intraFontUnload(g_font_small);
    intraFontShutdown();
    sceGuTerm();
}

/* ── Filled rect ─────────────────────────────────────────────── */
static void fill_rect(int x, int y, int w, int h, unsigned int col) {
    sceGuColor(col);
    Vert *v = (Vert*)sceGuGetMemory(2 * sizeof(Vert));
    v[0].x = x;     v[0].y = y;     v[0].z = 0;
    v[1].x = x + w; v[1].y = y + h; v[1].z = 0;
    sceGuDrawArray(GU_SPRITES,
                   GU_VERTEX_16BIT | GU_TRANSFORM_2D,
                   2, 0, v);
}

/* ── Progress / volume bar ───────────────────────────────────── */
static void draw_bar(int x, int y, int w, int h,
                     float frac, unsigned int fg, unsigned int bg) {
    if (frac < 0.f) frac = 0.f;
    if (frac > 1.f) frac = 1.f;
    fill_rect(x, y, w, h, bg);
    fill_rect(x, y, (int)(w * frac), h, fg);
}

/* ── Text helpers ────────────────────────────────────────────── */
static void draw_text(intraFont *fnt, int x, int y,
                      unsigned int col, const char *s) {
    if (!fnt) return;
    intraFontSetStyle(fnt, fnt->size, col, 0, 0, INTRAFONT_ALIGN_LEFT);
    intraFontPrint(fnt, x, y, s);
}

static void draw_text_center(intraFont *fnt, int y,
                              unsigned int col, const char *s) {
    if (!fnt) return;
    float w = intraFontMeasureText(fnt, s);
    draw_text(fnt, (int)(240 - w/2), y, col, s);
}

static void fmt_time(char *buf, int sec) {
    if (sec < 0) sec = 0;
    snprintf(buf, 12, "%d:%02d", sec/60, sec%60);
}

/* ── Main draw ───────────────────────────────────────────────── */
void ui_draw(PlayerState *ps) {
    sceGuStart(GU_DIRECT, g_list);

    /* Clear */
    sceGuClearColor(COL_BG);
    sceGuClear(GU_COLOR_BUFFER_BIT);

    /* Header bar */
    fill_rect(0, 0, SCREEN_W, 26, COL_HEADER);
    draw_text(g_font, 10, 18, COL_ACCENT, "PSP Music Player");

    /* Shuffle / Repeat badges */
    char badge[32];
    snprintf(badge, sizeof(badge), ps->shuffle ? "[SHF]" : "[---]");
    draw_text(g_font_small, 360, 18, ps->shuffle ? COL_ACCENT : COL_DIM, badge);

    const char *rep[] = {"[---]", "[ONE]", "[ALL]"};
    draw_text(g_font_small, 415, 18,
              ps->repeat ? COL_ACCENT : COL_DIM, rep[ps->repeat]);

    if (ps->track_count == 0) {
        draw_text_center(g_font, 120, COL_DIM, "No music found in ms0:/MUSIC/");
        draw_text_center(g_font_small, 140, COL_DIM, "Add .mp3 .ogg .wav files");
        goto finish;
    }

    /* Track number */
    {
        char tc[20];
        snprintf(tc, sizeof(tc), "%d / %d", ps->current+1, ps->track_count);
        draw_text(g_font_small, 10, 44, COL_DIM, tc);
    }

    /* Title */
    draw_text_center(g_font, 50, COL_TEXT, ps->title[0] ? ps->title : "Unknown");

    /* Artist — Album */
    if (ps->artist[0]) {
        char info[200];
        if (ps->album[0])
            snprintf(info, sizeof(info), "%s  —  %s", ps->artist, ps->album);
        else
            snprintf(info, sizeof(info), "%s", ps->artist);
        draw_text_center(g_font_small, 68, COL_DIM, info);
    }

    /* Progress bar */
    if (ps->duration_sec > 0) {
        float frac = (float)ps->position_sec / (float)ps->duration_sec;
        draw_bar(20, 88, 440, 7, frac, COL_BAR_FG, COL_BAR_BG);

        char tp[12], td[12], tstr[28];
        fmt_time(tp, ps->position_sec);
        fmt_time(td, ps->duration_sec);
        snprintf(tstr, sizeof(tstr), "%s / %s", tp, td);
        draw_text_center(g_font_small, 104, COL_DIM, tstr);
    }

    /* Play / Pause */
    draw_text_center(g_font, 124,
                     ps->paused ? COL_PAUSE : COL_PLAY,
                     ps->paused ? "PAUSED" : "PLAYING");

    /* Speed & Pitch */
    {
        char sp[64];
        snprintf(sp, sizeof(sp), "Speed: %.2fx    Pitch: %+.1f st",
                 ps->speed, ps->pitch);
        draw_text_center(g_font, 152, COL_TEXT, sp);
    }

    /* Volume bar */
    {
        float v = (float)ps->volume / (float)PSP_AUDIO_VOLUME_MAX;
        draw_text(g_font_small, 20, 180, COL_DIM, "Vol");
        draw_bar(50, 173, 360, 7, v, COL_BAR_FG, COL_BAR_BG);
        char pct[8];
        snprintf(pct, sizeof(pct), "%3d%%", (int)(v*100));
        draw_text(g_font_small, 418, 180, COL_DIM, pct);
    }

finish:
    /* Hint bar */
    fill_rect(0, 248, SCREEN_W, 24, COL_HINT);
    draw_text_center(g_font_small, 263, COL_DIM,
        "L/R:Speed  Sq/Tri:Pitch  O:Reset  X:Play  Start:Shuf  Sel:Rep");

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}
