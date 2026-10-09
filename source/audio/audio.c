/* audio.c - NDSP dual-buffer audio output (Implementierung)
 *
 * Stereo-Kanal-Layout:
 *   Links  : Pure Data Mono-Ausgang  (float -> int16)
 *   Rechts : 2ppqn Sync-Puls  (sync_clock_fill intern; stumm wenn sync_muted)
 *
 * Kein separater sync_clock_fill()-Aufruf in main.c noetig:
 * fill_one_wave_buf() ruft sync_clock_fill() genau dann auf, wenn der
 * Buffer verbraucht wird - keine Race Condition.
 *
 * Compile-time Geometrie (Makefile -D Flags):
 *   NDSP_BUFCOUNT      = 2
 *   NDSP_BUFSIZE       = 1024   Stereo-Frames pro Buffer
 *   SAMPLERATE         = 44100
 *   PD_BLOCK_TICKS     = 8   -> 8*64 = 512 Samples pro libpd-Aufruf
 *   PD_CALLS_PER_BUF   = NDSP_BUFSIZE / PD_SAMPLES_PER_CALL  (= 2)
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

#define PD_SAMPLES_PER_CALL  (PD_BLOCK_TICKS * 64)

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

    int16_t  pd_out[PD_SAMPLES_PER_CALL];   /* PD mono output temp (short)  */
    int16_t  sync_tmp[NDSP_BUFSIZE];        /* Sync pulse temp buffer */
};

/* --- Internes Buffer-Fill ----------------------------------------------- */
static void fill_one_wave_buf(AudioCtx *ctx, int idx, const float *pd_in_buf)
{
    (void)pd_in_buf;   /* reserviert fuer spaeteren Audio-Eingang */
    int16_t *dst = ctx->pcm_data[idx];

    /* Sync-Puls generieren (oder Stille wenn muted) */
    bool muted = atomic_load(&ctx->sync_muted);
    if (ctx->clk && !muted) {
        sync_clock_fill(ctx->clk, ctx->sync_tmp, NDSP_BUFSIZE);
    } else {
        memset(ctx->sync_tmp, 0, NDSP_BUFSIZE * sizeof(int16_t));
    }

    /* PD-Verarbeitung in Bloecken + Interleave */
    int frame = 0;

    for (int call = 0; call < PD_CALLS_PER_BUF; call++) {

        int ret = libpd_process_short(PD_BLOCK_TICKS,
                                      NULL,               /* 0 Eingangskanaele */
                                      (short*)ctx->pd_out); /* 1 Ausgangskanal */
        if (ret != 0) {
            memset(ctx->pd_out, 0, sizeof(ctx->pd_out));
        }

        for (int s = 0; s < PD_SAMPLES_PER_CALL; s++, frame++) {
            dst[frame * 2 + 0] = ctx->pd_out[s];   /* L = PD (bereits int16) */
            dst[frame * 2 + 1] = ctx->sync_tmp[frame]; /* R = Sync            */
        }
    }

    DSP_FlushDataCache(dst, WAVE_BUF_BYTES);
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

    ndspWaveBuf *wb = &ctx->wave_buf[ctx->cur_buf];
    if (wb->status != NDSP_WBUF_DONE) return;

    fill_one_wave_buf(ctx, ctx->cur_buf, pd_in_buf);

    wb->status = NDSP_WBUF_FREE;
    ndspChnWaveBufAdd(0, wb);
    ctx->cur_buf = (ctx->cur_buf + 1) % NDSP_BUFCOUNT;
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
