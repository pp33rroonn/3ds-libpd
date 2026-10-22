/* options_menu.c - OPTIONS overlay on the top screen (see options_menu.h) */

#include "options_menu.h"
#include "draw.h"
#include "font5x8.h"
#include "theme.h"
#include "../sync/sync_clock.h"

#include <stdio.h>
#include <string.h>

#define CPU_TICKS_PER_SEC  268111856ULL
#define TAP_TIMEOUT_TICKS  (CPU_TICKS_PER_SEC * 2ULL)

#define MENU_BPM_MIN   20.0f
#define MENU_BPM_MAX  300.0f

#define REPEAT_DELAY  20   /* frames until the first repeat */
#define REPEAT_RATE    4   /* frames between repeats        */

#define PATCH_SETTLE_FRAMES 12   /* ~200 ms at 60 fps */
#define RESET_FLASH_FRAMES  45   /* RESET row confirmation, ~0.75 s */

#ifndef SYNC_PPQN
#  define SYNC_PPQN 2
#endif

/* Layout */
#define X_MARK    6
#define X_LABEL   18
/* value column widened to 12 cells so longer labels ("Cam Polygon", 11 chars)
 * fit with a one-char gap before the "<" arrow. */
#define X_VALUE   (X_LABEL + 12 * FONT_ADV_X)
#define Y_HEADER  4
#define Y_ROW0    22
/* Row pitch. The four Cam value-modes moved to the A-drawer, so the main list
 * is only MENU_MAIN_LAST+1 (15) rows and can breathe again: last main row
 * (MENU_CAM, 14) sits at y = 22 + 14*12 = 190; the status separator at
 * 190+12+2 = 204 and its text at 210 stay above the 240 px edge. */
#define ROW_H     12
#define VALUE_MAX_CHARS 44

bool        g_options_open = false;
OptionsMenu g_menu;

static const PatchList *s_pl = NULL;

static const char *const SYNC_LABELS[SYNC_MODE_COUNT] = {
    "Off (Stereo)", "1ppqn Korg MS", "2ppqn Volca/PO", "24ppqn MIDI"
};
static const uint32_t SYNC_PPQN_VALUES[SYNC_MODE_COUNT] = { 0, 1, 2, 24 };
static const char *const RATE_LABELS[SYNC_RATE_Q4_MAX] = {
    "x0.25", "x0.5", "x0.75", "x1", "x1.25", "x1.5", "x1.75", "x2"
};
static const char *const FADER_LABELS[FADER_LAYOUT_COUNT] = {
    "Horizontal", "Vertical", "Auto (patch vsl/hsl)"
};
static const char *const SPECAN_LABELS[SPECAN_MODE_COUNT] = {
    "Waveform", "Envelope", "Stereo", "Level",
    "ASCII Wave", "ASCII Rain", "ASCII Scope", "Braille Scope", "Phosphor", "Stereo Dots", "Stereo Glow", "Stereo Field"
};
/* CAM row: Aus / Fader (only cam-* floats) / Bild (GB image) / Bild+Matrix
 * (image + 16x16 brightness written to the PD array "cam-matrix") */
static const char *const CAM_LABELS[CAM_MODE_COUNT] = {
    "Off", "Fader", "Image", "Image+Matrix"
};
/* Camera source: Back (outer/SELECT_OUT1, default) or Front (inner/SELECT_IN1).
 * Shown as "Cam B" / "Cam F" per the requested labels. */
static const char *const CAM_SOURCE_LABELS[CAM_SOURCE_COUNT] = {
    "Cam B", "Cam F"
};
/* Cam Pixel: Off, then pixelation grid X1 fine .. X5 coarse (each cell -> one
 * value). index 0 = Off is the shared rule for all cam value-modes. */
static const char *const CAM_PIXEL_LABELS[CAM_PIXEL_COUNT] = {
    "Off", "X1", "X2", "X3", "X4", "X5"
};
/* Cam Polygon: Off, then low-poly triangle render X1 fine .. X5 coarse (each
 * triangle -> one value). Same Off-first ordering as Cam Pixel / Cam Glitch. */
static const char *const CAM_POLYGON_LABELS[CAM_POLYGON_COUNT] = {
    "Off", "X1", "X2", "X3", "X4", "X5"
};
/* Cam Glitch: Off, then X1..X5 intensity; stacks on top of the base render. */
static const char *const CAM_GLITCH_LABELS[CAM_GLITCH_COUNT] = {
    "Off", "X1", "X2", "X3", "X4", "X5"
};
/* Buffer FX ("Smooth" field, kept as g_cam_smooth internally) is a technique
 * ladder, not a single axis: each stage uses a DIFFERENT mix of spatial and
 * temporal cleanup (see cam_view.h for the full per-stage parameters).
 *   Off = strongest/cleanest: LUMA-LOCK. Per-frame luma normalization (cancels
 *         the auto-exposure brightness stepping that flips whole dither zones)
 *         plus a static-scene FREEZE with hysteresis (holds a still image while
 *         nothing moves, live-follows on real motion) + light unsharp. AWB
 *         untouched. Kills the residual still-camera flicker outright.
 *   X1  = 3-frame per-pixel TEMPORAL MEDIAN (the previous Off look).
 *   X2  = spatial median + unsharp + exposure damp.
 *   X3  = median + unsharp, no exposure damping.
 *   X4  = median + unsharp, weaker sharpen.
 *   X5  = the older Off look (hysteresis-led, heavier EMA).
 *   X6..X9 = progressively more temporal EMA / less hysteresis (older looks).
 *   X10 = exactly today's raw image (no stabilization at all).
 * Does not touch Glitch; Buffer FX stays combinable with it. */
static const char *const CAM_SMOOTH_LABELS[CAM_SMOOTH_COUNT] = {
    "Off", "X1", "X2", "X3", "X4", "X5", "X6", "X7", "X8", "X9", "X10"
};

/* A-drawer: the four Cam value-modes, in display order. CAM_SUB_ROWS maps a
 * drawer row (0..CAM_SUB_COUNT-1) back to its MenuRow so change_value_row()
 * can edit the right field; CAM_SUB_LABELS are the short drawer captions. */
static const int CAM_SUB_ROWS[CAM_SUB_COUNT] = {
    MENU_CAM_SOURCE, MENU_CAM_PIXEL, MENU_CAM_POLYGON, MENU_CAM_GLITCH,
    MENU_CAM_SMOOTH
};
static const char *const CAM_SUB_LABELS[CAM_SUB_COUNT] = {
    "Source", "Pixel", "Polygon", "Glitch", "Buffer FX"
};

/* ---- helpers ------------------------------------------------------------ */

static int wrap(int v, int lo, int hi)   /* inclusive range */
{
    int n = hi - lo + 1;
    if (n <= 0) return lo;
    return lo + (((v - lo) % n) + n) % n;
}

static float clamp_bpm(float bpm)
{
    if (bpm < MENU_BPM_MIN) bpm = MENU_BPM_MIN;
    if (bpm > MENU_BPM_MAX) bpm = MENU_BPM_MAX;
    return bpm;
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static int patch_count(void)
{
    return s_pl ? s_pl->count : 0;
}

/* Y position of a menu row. The former blank line between patches and settings
 * was dropped to make room for the two extra Cam rows on the 240 px screen. */
static int row_y(int row)
{
    return Y_ROW0 + row * ROW_H;
}

static void set_bpm(OptionsMenu *m, float bpm)
{
    m->bpm = clamp_bpm(bpm);
    m->bpm_changed = true;
}

static void tap_tempo(OptionsMenu *m)
{
    u64 now = svcGetSystemTick();

    if (m->tap_count > 0 &&
        now - m->tap_stamps[m->tap_count - 1] > TAP_TIMEOUT_TICKS)
        m->tap_count = 0;

    if (m->tap_count >= MENU_TAP_MAX) {
        memmove(&m->tap_stamps[0], &m->tap_stamps[1],
                sizeof(u64) * (MENU_TAP_MAX - 1));
        m->tap_count = MENU_TAP_MAX - 1;
    }
    m->tap_stamps[m->tap_count++] = now;

    if (m->tap_count < 2) return;
    u64 total = m->tap_stamps[m->tap_count - 1] - m->tap_stamps[0];
    if (total == 0) return;
    double avg = (double)total / (double)(m->tap_count - 1);
    set_bpm(m, (float)(60.0 * (double)CPU_TICKS_PER_SEC / avg));
}

/* Change the value of a specific row without moving the selection. Used both by
 * the main list (row = m->selected_row) and by the A-drawer (row = the Cam
 * sub-row being edited). */
static void change_value_row(OptionsMenu *m, int row, int dir)
{
    if (row >= MENU_PATCH_0 && row <= MENU_PATCH_4) {
        int n = patch_count();
        if (n <= 0) return;
        int slot = row - MENU_PATCH_0;
        /* every slot (Patch 1..5): "Empty" + all patches */
        m->patch_idx[slot] = wrap(m->patch_idx[slot] + dir, -1, n - 1);
        m->slot_patch_changed[slot] = true;
        m->slot_settle[slot]        = PATCH_SETTLE_FRAMES;
        return;
    }

    switch (row) {
        case MENU_TEMPO:
            set_bpm(m, m->bpm + (float)dir);
            break;
        case MENU_SYNC:
            m->sync_mode = wrap(m->sync_mode + dir, 0, SYNC_MODE_COUNT - 1);
            m->sync_changed = true;
            break;
        case MENU_SWING:
            m->swing_pct = clampi(m->swing_pct + dir, SYNC_SWING_MIN, SYNC_SWING_MAX);
            m->clock_changed = true;
            break;
        case MENU_RATE:
            m->rate_q4 = clampi(m->rate_q4 + dir, SYNC_RATE_Q4_MIN, SYNC_RATE_Q4_MAX);
            m->clock_changed = true;
            break;
        case MENU_RESET:
            m->clock_reset = true;
            m->reset_flash = RESET_FLASH_FRAMES;
            break;
        case MENU_SPECAN:
            m->specan_mode = wrap(m->specan_mode + dir, 0, SPECAN_MODE_COUNT - 1);
            break;
        case MENU_FADERS:
            m->fader_layout = wrap(m->fader_layout + dir, 0, FADER_LAYOUT_COUNT - 1);
            m->fader_layout_changed = true;
            break;
        case MENU_EYE:
            m->eye_idx = wrap(m->eye_idx + dir, 0, THEME_COUNT - 1);
            g_theme_idx = m->eye_idx;
            break;
        case MENU_MIC:
            m->mic_on = m->mic_on ? 0 : 1;   /* An/Aus */
            m->mic_changed = true;
            break;
        case MENU_CAM:
            m->cam_mode = wrap(m->cam_mode + dir, 0, CAM_MODE_COUNT - 1);
            m->cam_changed = true;
            break;
        case MENU_CAM_SOURCE:
            m->cam_source = wrap(m->cam_source + dir, 0, CAM_SOURCE_COUNT - 1);
            m->cam_source_changed = true;
            break;
        case MENU_CAM_PIXEL:
            /* 0 = Off, 1..5 (X1..X5); no hardware change, synced each frame */
            m->cam_pixel = wrap(m->cam_pixel + dir, 0, CAM_PIXEL_COUNT - 1);
            break;
        case MENU_CAM_POLYGON:
            /* 0 = Off, 1..5 (X1..X5); low-poly triangle render, synced each frame */
            m->cam_polygon = wrap(m->cam_polygon + dir, 0, CAM_POLYGON_COUNT - 1);
            break;
        case MENU_CAM_GLITCH:
            /* 0 = Off, 1..5 intensity; stacks on the base render */
            m->cam_glitch = wrap(m->cam_glitch + dir, 0, CAM_GLITCH_COUNT - 1);
            break;
        case MENU_CAM_SMOOTH:
            /* Buffer FX: 0 = Off (cleanest: Luma-Lock static freeze), X1 =
             * 3-frame temporal median, X2..X9 trade spatial cleanup for
             * temporal/hysteresis, X10 = raw today's look */
            m->cam_smooth = wrap(m->cam_smooth + dir, 0, CAM_SMOOTH_COUNT - 1);
            break;
        default:
            break;
    }
}

/* Main-list convenience wrapper: edit the currently selected row. */
static void change_value(OptionsMenu *m, int dir)
{
    change_value_row(m, m->selected_row, dir);
}

/* ---- public API ----------------------------------------------------------- */

void options_menu_set_patch_list(const PatchList *pl)
{
    s_pl = pl;
}

void options_menu_init(OptionsMenu *m)
{
    memset(m, 0, sizeof(*m));
    m->selected_row = MENU_PATCH_0;
    /* boot: every slot Empty, nothing is loaded automatically */
    for (int i = 0; i < MENU_PATCH_SLOTS; i++) m->patch_idx[i] = -1;

    m->bpm = 120.0f;
    m->sync_mode = 0;   /* boot: sync Off (stereo); enable it in OPTIONS */
    m->swing_pct = SYNC_SWING_MIN;   /* straight */
    m->rate_q4   = 4;                /* x1 */

    m->specan_mode   = 0;
    m->eye_idx       = g_theme_idx;

    /* Cam value-modes default to Off (index 0). cam_pixel / cam_polygon /
     * cam_glitch are all 0 from the memset above; set explicitly for clarity. */
    m->cam_pixel     = 0;   /* Off */
    m->cam_polygon   = 0;   /* Off */
    m->cam_smooth    = 0;   /* Buffer FX Off = clean/stabilized base (default) */

    m->cam_drawer_open = 0;   /* A-drawer starts closed */
    m->cam_sub_row     = 0;   /* Source */
}

void options_menu_input(OptionsMenu *m, u32 held, u32 down)
{
    if (!m) return;

    /* D-pad with auto-repeat: pressed this frame, or held long enough */
    const u32 dpad = KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT;
    u32 act = down & dpad;
    if (held & dpad) {
        if (act) {
            m->repeat_frames = 0;
        } else {
            m->repeat_frames++;
            if (m->repeat_frames >= REPEAT_DELAY &&
                (m->repeat_frames - REPEAT_DELAY) % REPEAT_RATE == 0)
                act = held & dpad;
        }
    } else {
        m->repeat_frames = 0;
    }

    /* ---- A-drawer open: D-pad moves inside the Cam sub-panel ------------- */
    if (m->cam_drawer_open) {
        /* A or B closes the drawer and returns focus to the main list. */
        if (down & (KEY_A | KEY_B)) {
            m->cam_drawer_open = 0;
            m->repeat_frames   = 0;
            return;
        }
        if (act & KEY_DUP)
            m->cam_sub_row = wrap(m->cam_sub_row - 1, 0, CAM_SUB_COUNT - 1);
        if (act & KEY_DDOWN)
            m->cam_sub_row = wrap(m->cam_sub_row + 1, 0, CAM_SUB_COUNT - 1);
        if (act & KEY_DLEFT)  change_value_row(m, CAM_SUB_ROWS[m->cam_sub_row], -1);
        if (act & KEY_DRIGHT) change_value_row(m, CAM_SUB_ROWS[m->cam_sub_row], +1);
        return;
    }

    /* ---- main list ------------------------------------------------------- */
    if (act & KEY_DUP)
        m->selected_row = wrap(m->selected_row - 1, 0, MENU_MAIN_LAST);
    if (act & KEY_DDOWN)
        m->selected_row = wrap(m->selected_row + 1, 0, MENU_MAIN_LAST);
    if (act & KEY_DLEFT)  change_value(m, -1);
    if (act & KEY_DRIGHT) change_value(m, +1);

    /* On the Cam row, A opens the sub-options drawer on the right. */
    if (m->selected_row == MENU_CAM && (down & KEY_A)) {
        m->cam_drawer_open = 1;
        m->repeat_frames   = 0;
        return;
    }

    if (m->selected_row == MENU_TEMPO) {
        if (down & KEY_L) set_bpm(m, m->bpm - 10.0f);
        if (down & KEY_R) set_bpm(m, m->bpm + 10.0f);
        if (down & KEY_Y) tap_tempo(m);
    }
}

const char *options_menu_patch_name(const OptionsMenu *m, int slot)
{
    if (!m || slot < 0 || slot >= MENU_PATCH_SLOTS) return NULL;
    int idx = m->patch_idx[slot];
    if (idx < 0 || idx >= patch_count()) return NULL;
    return s_pl->entries[idx].display;
}

int options_menu_patch_count(void)
{
    return patch_count();
}

bool options_menu_sync_on(const OptionsMenu *m)
{
    return m && m->sync_mode != 0;
}

uint32_t options_menu_ppqn(const OptionsMenu *m)
{
    if (!m || m->sync_mode <= 0 || m->sync_mode >= SYNC_MODE_COUNT)
        return (uint32_t)SYNC_PPQN;
    return SYNC_PPQN_VALUES[m->sync_mode];
}

const char *options_menu_sync_label(const OptionsMenu *m)
{
    int i = m ? m->sync_mode : 0;
    if (i < 0 || i >= SYNC_MODE_COUNT) i = 0;
    return SYNC_LABELS[i];
}

const char *options_menu_cam_label(const OptionsMenu *m)
{
    int i = m ? m->cam_mode : 0;
    if (i < 0 || i >= CAM_MODE_COUNT) i = 0;
    return CAM_LABELS[i];
}

void options_menu_tick(OptionsMenu *m)
{
    if (!m) return;
    for (int i = 0; i < MENU_PATCH_SLOTS; i++)
        if (m->slot_settle[i] > 0) m->slot_settle[i]--;
    if (m->reset_flash > 0) m->reset_flash--;
}

int options_menu_take_slot_request(OptionsMenu *m, int *out_patch_idx)
{
    if (!m) return -1;
    for (int i = 0; i < MENU_PATCH_SLOTS; i++) {
        if (!m->slot_patch_changed[i] || m->slot_settle[i] > 0) continue;
        m->slot_patch_changed[i] = false;
        if (out_patch_idx) *out_patch_idx = m->patch_idx[i];
        return i;
    }
    return -1;
}

/* ---- drawing -------------------------------------------------------------- */

/* One scroll-list row: "> Label    < value >" */
static void draw_row(u16 *fb, int row, const char *label, const char *value,
                     bool selected, bool empty)
{
    int y   = row_y(row);
    u16 bg  = TH.bg;
    u16 fg  = selected ? TH.accent : theme_fg_at(y);
    u16 vfg = empty ? TH.dim : fg;

    if (selected) draw_char(fb, X_MARK, y, '>', TH.accent, bg);
    draw_string(fb, X_LABEL, y, label, fg, bg);

    char buf[VALUE_MAX_CHARS + 1];
    snprintf(buf, sizeof(buf), "%s", value);

    int x = X_VALUE;
    draw_arrow_left(fb, x, y, vfg, bg);
    x += 2 * FONT_ADV_X;
    x = draw_string(fb, x, y, buf, vfg, bg);
    x += FONT_ADV_X;
    draw_arrow_right(fb, x, y, vfg, bg);
}

static void draw_separator(u16 *fb, int y)
{
    draw_hline(fb, 4, y, TOP_W - 8, theme_fg_at(y));
}

/* The Cam A-drawer: a small panel floating over the free right-hand side with
 * the four Cam value-modes. Uses the same B/W theme colours as the main menu
 * (TH.bg / theme_fg_at / TH.accent) - no extra accent palette. */
static void draw_cam_drawer(u16 *fb, const OptionsMenu *m)
{
    const int X = 248;
    const int W = TOP_W - X - 6;                                 /* 146 */
    const int Y = Y_ROW0 - 4;                                    /* 18  */
    const int H = 4 + FONT_ADV_Y + 4 + CAM_SUB_COUNT * 12 + 6;   /* 71  */
    u16 bg = TH.bg;

    draw_rect_filled(fb, X, Y, W, H, bg);
    draw_rect(fb, X, Y, W, H, theme_fg_at(Y));

    /* title + divider */
    draw_string(fb, X + 6, Y + 4, "CAM", TH.accent, bg);
    int ly = Y + 4 + FONT_ADV_Y + 2;
    draw_hline(fb, X + 4, ly, W - 8, theme_fg_at(ly));

    int ry = ly + 5;
    for (int i = 0; i < CAM_SUB_COUNT; i++) {
        bool sel = (m->cam_sub_row == i);
        u16  fg  = sel ? TH.accent : theme_fg_at(ry);

        const char *val = "";
        switch (CAM_SUB_ROWS[i]) {
            case MENU_CAM_SOURCE:
                val = CAM_SOURCE_LABELS[clampi(m->cam_source, 0, CAM_SOURCE_COUNT - 1)];
                break;
            case MENU_CAM_PIXEL:
                val = CAM_PIXEL_LABELS[clampi(m->cam_pixel, 0, CAM_PIXEL_COUNT - 1)];
                break;
            case MENU_CAM_POLYGON:
                val = CAM_POLYGON_LABELS[clampi(m->cam_polygon, 0, CAM_POLYGON_COUNT - 1)];
                break;
            case MENU_CAM_GLITCH:
                val = CAM_GLITCH_LABELS[clampi(m->cam_glitch, 0, CAM_GLITCH_COUNT - 1)];
                break;
            case MENU_CAM_SMOOTH:
                val = CAM_SMOOTH_LABELS[clampi(m->cam_smooth, 0, CAM_SMOOTH_COUNT - 1)];
                break;
            default: break;
        }

        if (sel) draw_char(fb, X + 4, ry, '>', TH.accent, bg);
        draw_string(fb, X + 12, ry, CAM_SUB_LABELS[i], fg, bg);
        /* value column at 10 cells so the 9-char "Buffer FX" label still clears
         * it with a one-char gap (drawer width W = 146 px fits label + value). */
        draw_string(fb, X + 12 + 10 * FONT_ADV_X, ry, val, fg, bg);
        ry += 12;
    }
}

void options_menu_draw(u16 *fb, const OptionsMenu *m, float bpm,
                       int dsp_pct, unsigned glitches)
{
    if (!fb || !m) return;
    u16 bg = TH.bg;
    char label[16], value[VALUE_MAX_CHARS + 1];

    draw_clear(fb, bg);

    /* header */
    draw_string(fb, X_MARK, Y_HEADER, "pd_3ds", theme_fg_at(Y_HEADER), bg);
    draw_string(fb, TOP_W - 6 - 9 * FONT_ADV_X, Y_HEADER, "[OPTIONS]",
                TH.accent, bg);
    draw_separator(fb, Y_HEADER + FONT_ADV_Y + 3);

    /* patch slots */
    int n = patch_count();
    for (int s = 0; s < MENU_PATCH_SLOTS; s++) {
        int row = MENU_PATCH_0 + s;
        const char *name = options_menu_patch_name(m, s);
        snprintf(label, sizeof(label), "Patch %d", s + 1);
        if (name) {
            snprintf(value, sizeof(value), "%.30s (%02d/%02d)", name,
                     m->patch_idx[s] + 1, n);
        } else {
            snprintf(value, sizeof(value), "Empty");
        }
        draw_row(fb, row, label, value, m->selected_row == row, name == NULL);
    }

    /* settings */
    if (m->tap_count > 0 && m->selected_row == MENU_TEMPO)
        snprintf(value, sizeof(value), "%.1f BPM (tap %d)", (double)bpm,
                 m->tap_count);
    else
        snprintf(value, sizeof(value), "%.1f BPM", (double)bpm);
    draw_row(fb, MENU_TEMPO, "Tempo", value, m->selected_row == MENU_TEMPO, false);

    draw_row(fb, MENU_SYNC, "Sync", options_menu_sync_label(m),
             m->selected_row == MENU_SYNC, false);

    if (m->swing_pct <= SYNC_SWING_MIN)
        snprintf(value, sizeof(value), "OFF");
    else
        snprintf(value, sizeof(value), "%d%% (clock-step)", m->swing_pct);
    draw_row(fb, MENU_SWING, "Swing", value, m->selected_row == MENU_SWING, false);

    int rq = clampi(m->rate_q4, SYNC_RATE_Q4_MIN, SYNC_RATE_Q4_MAX);
    snprintf(value, sizeof(value), "%s (sync pulse)", RATE_LABELS[rq - 1]);
    draw_row(fb, MENU_RATE, "Rate", value, m->selected_row == MENU_RATE,
             !options_menu_sync_on(m));

    draw_row(fb, MENU_RESET, "Clock",
             m->reset_flash > 0 ? "RESET -> beat 1" : "RESET",
             m->selected_row == MENU_RESET, false);

    int sa = (m->specan_mode >= 0 && m->specan_mode < SPECAN_MODE_COUNT)
           ? m->specan_mode : 0;
    draw_row(fb, MENU_SPECAN, "SpecAn", SPECAN_LABELS[sa],
             m->selected_row == MENU_SPECAN, false);

    int fl = (m->fader_layout >= 0 && m->fader_layout < FADER_LAYOUT_COUNT)
           ? m->fader_layout : 0;
    draw_row(fb, MENU_FADERS, "Faders", FADER_LABELS[fl],
             m->selected_row == MENU_FADERS, false);

    int ei = (m->eye_idx >= 0 && m->eye_idx < THEME_COUNT) ? m->eye_idx : 0;
    draw_row(fb, MENU_EYE, "Theme", g_themes[ei].name,
             m->selected_row == MENU_EYE, false);

    draw_row(fb, MENU_MIC, "Mic", m->mic_on ? "On" : "Off",
             m->selected_row == MENU_MIC, false);

    int cm = (m->cam_mode >= 0 && m->cam_mode < CAM_MODE_COUNT) ? m->cam_mode : 0;
    draw_row(fb, MENU_CAM, "Cam", CAM_LABELS[cm],
             m->selected_row == MENU_CAM, false);
    /* The four Cam value-modes (Source/Pixel/Polygon/Glitch) now live in the
     * A-drawer. When the Cam row is selected and the drawer is closed, hint "A"
     * so the sub-panel is discoverable. */
    if (m->selected_row == MENU_CAM && !m->cam_drawer_open)
        draw_string(fb, X_VALUE + 15 * FONT_ADV_X, row_y(MENU_CAM), "[A]",
                    TH.accent, bg);

    /* status line (controls are documented in options_menu.h / README) */
    int y = row_y(MENU_MAIN_LAST) + ROW_H + 2;
    draw_separator(fb, y);
    y += 6;
    draw_stringf(fb, X_LABEL, y, theme_fg_at(y), bg, "DSP: %d%%   Glitch: %u",
                 dsp_pct, glitches);

    /* Cam sub-options panel floats over the free right-hand side. */
    if (m->cam_drawer_open) draw_cam_drawer(fb, m);
}
