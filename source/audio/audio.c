/* audio.c - NDSP dual-buffer audio output (Implementierung)
 *
 * Stereo-Kanal-Layout (libpd liefert 2 Kanaele interleaved: l, r, l, r, ...):
 *   Sync AN : Links  = PD-Mono-Mix (l+r)/2, Rechts = Sync-Puls
 *   Sync AUS: Links  = PD links,           Rechts = PD rechts (echtes Stereo)
 *
 * fill_one_wave_buf() advances the global clock (sync_clock_render) in
 * 64-sample steps right before each Pd tick and sends clock-* to PD.
 *
 * Compile-time Geometrie (Makefile -D Flags):
 *   NDSP_BUFCOUNT      = 4   (Reserve gegen Aussetzer, ~93 ms)
 *   NDSP_BUFSIZE       = 1024   Stereo-Frames pro Buffer
 *   SAMPLERATE         = 44100
 *   PD_BLOCK_TICKS     = 8   (only checks the buffer geometry now; PD is
 *                             processed one 64-sample tick at a time so the
 *                             clock-* messages land on the right Pd tick)
 */

#include "audio.h"
#include "../sync/sync_clock.h"

/* <ndsp/ndsp.h> already included via audio.h -> <3ds.h> */
#include <z_libpd.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>

/* --- Compile-time Defaults ---------------------------------------------- */
#ifndef PD_BLOCK_TICKS
#  define PD_BLOCK_TICKS 8
#endif
#ifndef NDSP_BUFSIZE
#  define NDSP_BUFSIZE 1024
#endif
#ifndef NDSP_BUFCOUNT
#  define NDSP_BUFCOUNT 2
#endif
#ifndef SAMPLERATE
#  define SAMPLERATE 44100
#endif

#define PD_BLOCK_SIZE        64   /* one Pd tick */
#define PD_SAMPLES_PER_CALL  (PD_BLOCK_TICKS * PD_BLOCK_SIZE)

#if (NDSP_BUFSIZE % PD_SAMPLES_PER_CALL) != 0
#  error "NDSP_BUFSIZE muss Vielfaches von (PD_BLOCK_TICKS * 64) sein"
#endif

#define PD_CALLS_PER_BUF   (NDSP_BUFSIZE / PD_SAMPLES_PER_CALL)
#define WAVE_BUF_BYTES     (NDSP_BUFSIZE * 2 * sizeof(int16_t))

/* --- float -> int16 mit sofortigem Clampen ------------------------------ */
static inline int16_t f32_to_i16(float f)
{
    float s = f * 32767.0f;
    if (s >  32767.0f) s =  32767.0f;
    if (s < -32768.0f) s = -32768.0f;
    return (int16_t)s;
}

/* --- AudioCtx ----------------------------------------------------------- */
struct AudioCtx {
    ndspWaveBuf      wave_buf[NDSP_BUFCOUNT];
    int16_t         *pcm_data[NDSP_BUFCOUNT];
    SyncClock        *clk;
    int               cur_buf;
    bool              running;

    /* Sync-Mute: atomic damit audio_set_sync_enabled() aus main-Loop
     * sicher in fill_one_wave_buf() gelesen werden kann. */
    _Atomic bool      sync_muted;

    int16_t  pd_out[PD_SAMPLES_PER_CALL * 2]; /* PD stereo output, interleaved */
    int16_t  sync_tmp[NDSP_BUFSIZE];        /* Sync pulse temp buffer */

    /* Diagnose: groesster PD-Ausschlag (L/R) seit letzter Abfrage */
    _Atomic int       peak_l, peak_r;

    /* Diagnose: wie oft die NDSP-Warteschlange leergelaufen ist
     * (= hoerbares Knacken/Aussetzer). Zaehlt seit Programmstart. */
    unsigned          underruns;

    /* Diagnose: DSP-Last in Prozent (Rechenzeit / Abspieldauer eines
     * Buffers), geglaettet. >100 = PD rechnet langsamer als Echtzeit. */
    _Atomic int       load_pct;

    /* Scope: ring of the most recent PD output frames (L/R, before the
     * sync pulse is mixed in). Written here, read by audio_get_scope() from
     * the same (main) thread -> no locking needed. */
    int16_t           scope_l[AUDIO_SCOPE_FRAMES];
    int16_t           scope_r[AUDIO_SCOPE_FRAMES];
    int               scope_pos;   /* next write index */
};

/* Abspieldauer eines Buffers in System-Ticks (Tick-Takt ist fest,
 * unabhaengig vom New-3DS-Turbo) */
#define BUF_TICKS ((uint64_t)NDSP_BUFSIZE * SYSCLOCK_ARM11 / SAMPLERATE)

/* --- Global clock -> PD -------------------------------------------------- *
 * Not slot-renamed: every loaded patch receives these.
 *   clock-step : 16th counter since RESET (0,1,2,...), swing applied
 *   clock-tick : position in the bar in 24-ppqn ticks (0..95)
 *   clock-bpm  : current tempo (on change, RESET and after a patch load)
 *   clock-ms   : milliseconds per 16th (sent together with clock-bpm)   */
static void send_clock_events(const SyncClockEvents *ev)
{
    if (ev->info) {
        libpd_float("clock-bpm", ev->bpm);
        libpd_float("clock-ms", 15000.0f / ev->bpm);
    }
    if (ev->ticks)
        libpd_float("clock-tick", (float)(ev->tick % (SYNC_CLOCK_PPQN * 4)));
    if (ev->steps)
        libpd_float("clock-step", (float)ev->step);
}

/* --- Internes Buffer-Fill ----------------------------------------------- */
static void fill_one_wave_buf(AudioCtx *ctx, int idx, const float *pd_in_buf)
{
    (void)pd_in_buf;   /* reserviert fuer spaeteren Audio-Eingang */
    int16_t *dst = ctx->pcm_data[idx];

    /* PD runs in 64-sample blocks (1 Pd tick). Before each block the master
     * clock advances by 64 samples and its events go to ALL patches as
     * clock-* messages -> timing resolution 1.45 ms. The sync pulse (right
     * channel) is written sample-accurately by the same clock. */
    bool sync_on = !atomic_load(&ctx->sync_muted);
    int frame = 0;
    int pk_l = 0, pk_r = 0;

    for (int blk = 0; blk < NDSP_BUFSIZE / PD_BLOCK_SIZE; blk++) {
        if (ctx->clk) {
            SyncClockEvents ev;
            sync_clock_render(ctx->clk, sync_on ? &ctx->sync_tmp[frame] : NULL,
                              PD_BLOCK_SIZE, &ev);
            send_clock_events(&ev);
        } else if (sync_on) {
            memset(&ctx->sync_tmp[frame], 0, PD_BLOCK_SIZE * sizeof(int16_t));
        }

        if (libpd_process_short(1, NULL, (short *)ctx->pd_out) != 0)
            memset(ctx->pd_out, 0, sizeof(ctx->pd_out));

        for (int s = 0; s < PD_BLOCK_SIZE; s++, frame++) {
            int16_t l = ctx->pd_out[s * 2 + 0];
            int16_t r = ctx->pd_out[s * 2 + 1];
            int al = l < 0 ? -(int)l : l, ar = r < 0 ? -(int)r : r;
            if (al > pk_l) pk_l = al;
            if (ar > pk_r) pk_r = ar;
            ctx->scope_l[ctx->scope_pos] = l;
            ctx->scope_r[ctx->scope_pos] = r;
            ctx->scope_pos = (ctx->scope_pos + 1) % AUDIO_SCOPE_FRAMES;
            if (sync_on) {
                /* L = PD mono mix, R = sync pulse */
                dst[frame * 2 + 0] = (int16_t)(((int32_t)l + (int32_t)r) / 2);
                dst[frame * 2 + 1] = ctx->sync_tmp[frame];
            } else {
                /* sync off: PD stereo straight through */
                dst[frame * 2 + 0] = l;
                dst[frame * 2 + 1] = r;
            }
        }
    }

    if (pk_l > atomic_load(&ctx->peak_l)) atomic_store(&ctx->peak_l, pk_l);
    if (pk_r > atomic_load(&ctx->peak_r)) atomic_store(&ctx->peak_r, pk_r);

    DSP_FlushDataCache(dst, WAVE_BUF_BYTES);
}

void audio_get_pd_peak(AudioCtx *ctx, int *l, int *r)
{
    *l = ctx ? atomic_exchange(&ctx->peak_l, 0) : 0;
    *r = ctx ? atomic_exchange(&ctx->peak_r, 0) : 0;
}

int audio_get_scope(const AudioCtx *ctx, int16_t *l, int16_t *r, int n)
{
    if (!ctx || !l || !r || n <= 0) return 0;
    if (n > AUDIO_SCOPE_FRAMES) n = AUDIO_SCOPE_FRAMES;
    int start = (ctx->scope_pos - n + AUDIO_SCOPE_FRAMES) % AUDIO_SCOPE_FRAMES;
    for (int i = 0; i < n; i++) {
        int k = (start + i) % AUDIO_SCOPE_FRAMES;
        l[i] = ctx->scope_l[k];
        r[i] = ctx->scope_r[k];
    }
    return n;
}

/* --- Oeffentliche API --------------------------------------------------- */

AudioCtx *audio_init(SyncClock *clk)
{
    Result rc = ndspInit();
    if (R_FAILED(rc)) {
        fprintf(stderr, "[audio] ndspInit() fehlgeschlagen: 0x%08lX\n", rc);
        return NULL;
    }

    AudioCtx *ctx = calloc(1, sizeof(AudioCtx));
    if (!ctx) { ndspExit(); return NULL; }

    ctx->clk     = clk;
    ctx->cur_buf = 0;
    atomic_store(&ctx->sync_muted, false);

    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, (float)SAMPLERATE);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);

    float mix[12] = {0};
    mix[0] = 1.0f;  /* L -> L */
    mix[1] = 1.0f;  /* R -> R */
    ndspChnSetMix(0, mix);

    for (int i = 0; i < NDSP_BUFCOUNT; i++) {
        ctx->pcm_data[i] = (int16_t *)linearAlloc(WAVE_BUF_BYTES);
        if (!ctx->pcm_data[i]) {
            fprintf(stderr, "[audio] linearAlloc fehlgeschlagen (buf %d)\n", i);
            for (int j = 0; j < i; j++) linearFree(ctx->pcm_data[j]);
            free(ctx);
            ndspExit();
            return NULL;
        }
        memset(ctx->pcm_data[i], 0, WAVE_BUF_BYTES);
        DSP_FlushDataCache(ctx->pcm_data[i], WAVE_BUF_BYTES);

        memset(&ctx->wave_buf[i], 0, sizeof(ndspWaveBuf));
        ctx->wave_buf[i].data_vaddr = ctx->pcm_data[i];
        ctx->wave_buf[i].nsamples   = NDSP_BUFSIZE;
        ctx->wave_buf[i].looping    = false;
        ctx->wave_buf[i].status     = NDSP_WBUF_FREE;
    }

    /* Pre-Roll: beide Buffer befuellen und in NDSP einreihen */
    for (int i = 0; i < NDSP_BUFCOUNT; i++) {
        fill_one_wave_buf(ctx, i, NULL);
        ndspChnWaveBufAdd(0, &ctx->wave_buf[i]);
    }

    ctx->running = true;
    return ctx;
}

void audio_fill_buffer(AudioCtx *ctx, const float *pd_in_buf)
{
    if (!ctx || !ctx->running) return;

    /* Alle fertig abgespielten Buffer nachfuellen, nicht nur einen.
     * Der Main-Loop laeuft mit 60 Hz; dauert ein Frame laenger (Bildschirm-
     * Ausgabe, Patch laden), sind evtl. mehrere Buffer gleichzeitig DONE. */
    int done = 0;
    for (int i = 0; i < NDSP_BUFCOUNT; i++)
        if (ctx->wave_buf[i].status == NDSP_WBUF_DONE) done++;
    if (done == NDSP_BUFCOUNT) ctx->underruns++;   /* Warteschlange war leer */

    /* Hoechstens NDSP_BUFCOUNT Buffer pro Aufruf: Rechnet PD langsamer als
     * Echtzeit (CPU-Ueberlast), wird sonst schon der naechste Buffer fertig,
     * bevor dieser gefuellt ist -> Endlosschleife, App haengt. */
    for (int n = 0; n < NDSP_BUFCOUNT &&
                    ctx->wave_buf[ctx->cur_buf].status == NDSP_WBUF_DONE; n++) {
        ndspWaveBuf *wb = &ctx->wave_buf[ctx->cur_buf];
        uint64_t t0 = svcGetSystemTick();
        fill_one_wave_buf(ctx, ctx->cur_buf, pd_in_buf);
        uint64_t dt = svcGetSystemTick() - t0;
        int pct = (int)(dt * 100 / BUF_TICKS);
        int old = atomic_load(&ctx->load_pct);
        atomic_store(&ctx->load_pct, (old * 7 + pct) / 8);   /* glaetten */
        wb->status = NDSP_WBUF_FREE;
        ndspChnWaveBufAdd(0, wb);
        ctx->cur_buf = (ctx->cur_buf + 1) % NDSP_BUFCOUNT;
    }
}

int audio_get_dsp_load(const AudioCtx *ctx)
{
    return ctx ? atomic_load(&((AudioCtx *)ctx)->load_pct) : 0;
}

void audio_reset_dsp_load(AudioCtx *ctx)
{
    if (ctx) atomic_store(&ctx->load_pct, 0);
}

unsigned audio_get_underruns(const AudioCtx *ctx)
{
    return ctx ? ctx->underruns : 0;
}

void audio_set_sync_enabled(AudioCtx *ctx, bool sync_on)
{
    if (ctx) atomic_store(&ctx->sync_muted, !sync_on);
}

void audio_deinit(AudioCtx *ctx)
{
    if (!ctx) return;
    ctx->running = false;
    ndspChnReset(0);
    ndspExit();
    for (int i = 0; i < NDSP_BUFCOUNT; i++) {
        if (ctx->pcm_data[i]) linearFree(ctx->pcm_data[i]);
    }
    free(ctx);
}

bool audio_is_running(const AudioCtx *ctx)
{
    return ctx ? ctx->running : false;
}
