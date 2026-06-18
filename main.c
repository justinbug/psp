/*
 * PSP Music Player with Pitch & Speed Control
 * For PSP 2000 (Slim) - uses 64MB RAM mode
 * Supports: MP3, OGG, WAV files from ms0:/MUSIC/
 *
 * Controls:
 *   Cross      - Play/Pause
 *   Left/Right - Previous/Next track
 *   Up/Down    - Volume up/down
 *   L Trigger  - Decrease speed
 *   R Trigger  - Increase speed
 *   Square     - Decrease pitch
 *   Triangle   - Increase pitch
 *   Circle     - Reset pitch & speed
 *   Start      - Shuffle on/off
 *   Select     - Repeat mode
 */

#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspaudio.h>
#include <pspaudiolib.h>
#include <pspgu.h>
#include <pspgum.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <dirent.h>

#include "audio.h"
#include "ui.h"
#include "player.h"

PSP_MODULE_INFO("PSP Music Player", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
// Use extra RAM on PSP 2000/3000 (64MB)
PSP_HEAP_SIZE_KB(49152); // ~48MB heap — leaves room for OS + audio buffers

#define MAX_TRACKS 512
#define MUSIC_PATH "ms0:/MUSIC"

// ── Global state ──────────────────────────────────────────────────
PlayerState g_player;

// ── Exit callback ─────────────────────────────────────────────────
int exit_callback(int arg1, int arg2, void *common) {
    g_player.running = 0;
    return 0;
}

int callback_thread(SceSize args, void *argp) {
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

void setup_callbacks(void) {
    int thid = sceKernelCreateThread("update_thread", callback_thread,
                                      0x11, 0xFA0, 0, 0);
    if (thid >= 0) sceKernelStartThread(thid, 0, 0);
}

// ── File scanning ─────────────────────────────────────────────────
int scan_music_files(char tracks[][MAX_PATH_LEN], int max) {
    DIR *dir = opendir(MUSIC_PATH);
    if (!dir) return 0;

    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < max) {
        const char *name = entry->d_name;
        int len = strlen(name);
        if (len < 4) continue;

        const char *ext = name + len - 4;
        int valid = (strcasecmp(ext, ".mp3") == 0 ||
                     strcasecmp(ext, ".ogg") == 0 ||
                     strcasecmp(ext+1, ".wav") == 0);
        if (valid) {
            snprintf(tracks[count], MAX_PATH_LEN,
                     "%s/%s", MUSIC_PATH, name);
            count++;
        }
    }
    closedir(dir);
    return count;
}

// ── Button handling ───────────────────────────────────────────────
static u32 prev_buttons = 0;

int button_pressed(SceCtrlData *pad, u32 btn) {
    return (pad->Buttons & btn) && !(prev_buttons & btn);
}

void handle_input(SceCtrlData *pad) {
    // Play / Pause
    if (button_pressed(pad, PSP_CTRL_CROSS))
        player_toggle_pause(&g_player);

    // Next / Previous
    if (button_pressed(pad, PSP_CTRL_RIGHT))
        player_next(&g_player);
    if (button_pressed(pad, PSP_CTRL_LEFT))
        player_prev(&g_player);

    // Volume
    if (pad->Buttons & PSP_CTRL_UP)
        player_volume_up(&g_player);
    if (pad->Buttons & PSP_CTRL_DOWN)
        player_volume_down(&g_player);

    // Speed  (L/R triggers, held = continuous)
    if (pad->Buttons & PSP_CTRL_LTRIGGER)
        player_speed_dec(&g_player);
    if (pad->Buttons & PSP_CTRL_RTRIGGER)
        player_speed_inc(&g_player);

    // Pitch  (Square / Triangle, held = continuous)
    if (pad->Buttons & PSP_CTRL_SQUARE)
        player_pitch_dec(&g_player);
    if (pad->Buttons & PSP_CTRL_TRIANGLE)
        player_pitch_inc(&g_player);

    // Reset pitch & speed
    if (button_pressed(pad, PSP_CTRL_CIRCLE))
        player_reset_fx(&g_player);

    // Shuffle
    if (button_pressed(pad, PSP_CTRL_START))
        player_toggle_shuffle(&g_player);

    // Repeat
    if (button_pressed(pad, PSP_CTRL_SELECT))
        player_cycle_repeat(&g_player);

    prev_buttons = pad->Buttons;
}

// ── Entry point ───────────────────────────────────────────────────
int main(void) {
    setup_callbacks();

    // Init display
    ui_init();

    // Init audio subsystem
    pspAudioInit();
    audio_init();

    // Initialise player state
    memset(&g_player, 0, sizeof(PlayerState));
    g_player.running   = 1;
    g_player.volume    = PSP_AUDIO_VOLUME_MAX;
    g_player.speed     = 1.0f;   // 1.0 = normal
    g_player.pitch     = 0.0f;   // 0 semitones shift
    g_player.repeat    = REPEAT_NONE;
    g_player.shuffle   = 0;
    g_player.paused    = 1;

    // Scan for music
    g_player.track_count = scan_music_files(g_player.tracks, MAX_TRACKS);
    g_player.current     = 0;

    if (g_player.track_count > 0)
        player_load(&g_player, 0);

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    // ── Main loop ─────────────────────────────────────────────────
    while (g_player.running) {
        SceCtrlData pad;
        sceCtrlReadBufferPositive(&pad, 1);
        handle_input(&pad);

        audio_update(&g_player);   // decode + pitch/speed process + queue
        ui_draw(&g_player);        // render frame
        player_check_end(&g_player); // auto-advance

        sceDisplayWaitVblankStart();
    }

    // Cleanup
    audio_shutdown();
    pspAudioEnd();
    ui_shutdown();

    sceKernelExitGame();
    return 0;
}
