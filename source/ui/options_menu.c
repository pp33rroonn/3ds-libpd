/* options_menu.c - OPTIONS overlay on the top screen (see options_menu.h) */

#include "options_menu.h"
#include "draw.h"
#include "font5x8.h"
#include "theme.h"

#include <stdio.h>
#include <string.h>

#define CPU_TICKS_PER_SEC  268111856ULL
#define TAP_TIMEOUT_TICKS  (CPU_TICKS_PER_SEC * 2ULL)

#define MENU_BPM_MIN   20.0f
#define MENU_BPM_MAX  300.0f

#define REPEAT_DELAY  20   /* frames until the first repeat */
#define REPEAT_RATE    4   /* frames between repeats        */

#define PATCH_SETTLE_FRAMES 12   /* ~200 ms at 60 fps */

#ifndef SYNC_PPQN
#  define SYNC_PPQN 2
#endif

/* Layout */
#define X_MARK    6
#define X_LABEL   18
#define X_VALUE   (X_LABEL + 9 * FONT_ADV_X)
#define Y_HEADER  4
#define Y_ROW0    22
#define ROW_H     13
#define VALUE_MAX_CHARS 44

bool        g_options_open = false;
OptionsMenu g_menu;

static const PatchList *s_pl = NULL;

static const char *const SYNC_LABELS[SYNC_MODE_COUNT] = {
    "Off (Stereo)", "1ppqn Korg MS", "2ppqn Volca/PO", "24ppqn MIDI"
};
static const uint32_t SYNC_PPQN_VALUES[SYNC_MODE_COUNT] = { 0, 1, 2, 24 };
static const char *const SPECAN_LABELS[SPECAN_MODE_COUNT] = {
    "Waveform", "Envelope", "Stereo", "Level",
    "ASCII Wave", "ASCII Rain", "ASCII Scope", "Braille Scope", "Phosphor", "Stereo Dots", "Stereo Glow", "Stereo Field"
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

static int patch_count(void)
{
    return s_pl ? s_pl->count : 0;
}

/* Y position of a menu row (one blank line between patches and settings) */
static int row_y(int row)
{
    int line = row < MENU_TEMPO ? row : row + 1;
    return Y_ROW0 + line * ROW_H;
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

static void change_value(OptionsMenu *m, int dir)
{
    int row = m->selected_row;

    if (row >= MENU_PATCH_0 && row <= MENU_PATCH_4) {
        int n = patch_count();
        if (n <= 0) return;
        int slot = row - MENU_PATCH_0;
        if (slot == 0) {
            /* slot 0 is the running patch: never Empty */
            int cur = m->patch_idx[0] < 0 ? 0 : m->patch_idx[0];
            m->patch_idx[0]  = wrap(cur + dir, 0, n - 1);
            m->patch_pending = m->patch_idx[0];
            m->patch_settle  = PATCH_SETTLE_FRAMES;
        } else {
            /* slots 1..4: "Off" + all patches */
            m->patch_idx[slot] = wrap(m->patch_idx[slot] + dir, -1, n - 1);
            m->slot_patch_idx[slot - 1]     = m->patch_idx[slot] + 1;
            m->slot_patch_changed[slot - 1] = true;
            m->slot_settle[slot - 1]        = PATCH_SETTLE_FRAMES;
        }
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
        case MENU_SPECAN:
            m->specan_mode = wrap(m->specan_mode + dir, 0, SPECAN_MODE_COUNT - 1);
            break;
        case MENU_EYE:
            m->eye_idx = wrap(m->eye_idx + dir, 0, THEME_COUNT - 1);
            g_theme_idx = m->eye_idx;
            break;
        default:
            break;
    }
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
    for (int i = 0; i < MENU_PATCH_SLOTS; i++) m->patch_idx[i] = -1;
    if (patch_count() > 0) m->patch_idx[0] = s_pl->current;

    m->bpm = 120.0f;
    m->sync_mode = 2;
    for (int i = 1; i < SYNC_MODE_COUNT; i++)
        if (SYNC_PPQN_VALUES[i] == (uint32_t)SYNC_PPQN) m->sync_mode = i;

    m->specan_mode   = 0;
    m->eye_idx       = g_theme_idx;
    m->patch_pending = -1;
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

    if (act & KEY_DUP)
        m->selected_row = wrap(m->selected_row - 1, 0, MENU_ROW_COUNT - 1);
    if (act & KEY_DDOWN)
        m->selected_row = wrap(m->selected_row + 1, 0, MENU_ROW_COUNT - 1);
    if (act & KEY_DLEFT)  change_value(m, -1);
    if (act & KEY_DRIGHT) change_value(m, +1);

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

int options_menu_take_patch_request(OptionsMenu *m)
{
    if (!m) return -1;
    for (int i = 0; i < MENU_PATCH_SLOTS - 1; i++)
        if (m->slot_settle[i] > 0) m->slot_settle[i]--;

    if (m->patch_pending < 0) return -1;
    if (--m->patch_settle > 0) return -1;
    int idx = m->patch_pending;
    m->patch_pending = -1;
    return idx;
}

int options_menu_take_slot_request(OptionsMenu *m, int *out_patch_idx)
{
    if (!m) return -1;
    for (int i = 0; i < MENU_PATCH_SLOTS - 1; i++) {
        if (!m->slot_patch_changed[i] || m->slot_settle[i] > 0) continue;
        m->slot_patch_changed[i] = false;
        if (out_patch_idx) *out_patch_idx = m->slot_patch_idx[i] - 1;
        return i + 1;
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
            snprintf(value, sizeof(value), s == 0 ? "Empty" : "Off");
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

    int sa = (m->specan_mode >= 0 && m->specan_mode < SPECAN_MODE_COUNT)
           ? m->specan_mode : 0;
    draw_row(fb, MENU_SPECAN, "SpecAn", SPECAN_LABELS[sa],
             m->selected_row == MENU_SPECAN, false);

    int ei = (m->eye_idx >= 0 && m->eye_idx < THEME_COUNT) ? m->eye_idx : 0;
    draw_row(fb, MENU_EYE, "Eye", g_themes[ei].name,
             m->selected_row == MENU_EYE, false);

    /* status + help */
    int y = row_y(MENU_ROW_COUNT - 1) + ROW_H + 2;
    draw_separator(fb, y);
    y += 6;
    draw_stringf(fb, X_LABEL, y, theme_fg_at(y), bg, "DSP: %d%%   Glitch: %u",
                 dsp_pct, glitches);
    y += FONT_ADV_Y + 4;
    draw_separator(fb, y);
    y += 6;
    draw_string(fb, X_LABEL, y, "D-pad: navigate / change    START: close",
                TH.dim, bg);
    y += ROW_H;
    draw_string(fb, X_LABEL, y, "Y: Tap-Tempo (on Tempo row)   L/R: -10/+10 BPM",
                TH.dim, bg);
}
