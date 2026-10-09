#pragma once
/* audio.h - NDSP dual-buffer audio output
 *
 * Stereo-Kanalbelegung (fest):
 *   Links  [0] : PD Mono-Audio-Ausgang (float -> int16)
 *   Rechts [1] : 2ppqn Sync-Puls  (SyncClock intern; stumm via audio_set_sync_enabled)
 *
 * Buffer-Geometrie (via Makefile -D Flags):
 *   NDSP_BUFSIZE   : Stereo-Frames pro Half-Buffer  (z.B. 1024)
 *   NDSP_BUFCOUNT  : Anzahl Wave-Buffer              (immer 2)
 *   SAMPLERATE     : Samplerate in Hz               (44100)
 *   PD_BLOCK_TICKS : libpd ticks pro Fill-Aufruf
 *                    NDSP_BUFSIZE muss Vielfaches von (PD_BLOCK_TICKS * 64) sein
 */

#include <3ds.h>
#include <stdbool.h>
#include <stdint.h>
#include "../sync/sync_clock.h"

typedef struct AudioCtx AudioCtx;

/** audio_init - NDSP initialisieren, Wave-Buffer allokieren.
 *  @param clk  SyncClock fuer rechten Kanal (NULL = stumm). */
AudioCtx *audio_init(SyncClock *clk);

/** audio_fill_buffer - Naechsten freien NDSP-Buffer befuellen.
 *  Einmal pro Frame aus dem Main-Loop aufrufen.
 *  Gibt sofort zurueck wenn Buffer noch nicht abgespielt. */
void audio_fill_buffer(AudioCtx *ctx, const float *pd_in_buf);

/** audio_set_sync_enabled - Sync-Puls auf rechtem Kanal ein/ausschalten.
 *  Atomic: sicher aus Main-Loop aenderbar waehrend Audio laeuft. */
void audio_set_sync_enabled(AudioCtx *ctx, bool sync_on);

/** audio_deinit - NDSP stoppen, alle Ressourcen freigeben. */
void audio_deinit(AudioCtx *ctx);

/** audio_is_running - true solange NDSP aktiv. */
bool audio_is_running(const AudioCtx *ctx);
