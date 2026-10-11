#pragma once
/* audio.h - NDSP dual-buffer audio output
 *
 * Stereo-Kanalbelegung:
 *   Sync AN : Links = PD-Mono-Mix, Rechts = Sync-Puls
 *   Sync AUS: Links = PD links,    Rechts = PD rechts (Stereo)
 *
 * Buffer-Geometrie (via Makefile -D Flags):
 *   NDSP_BUFSIZE   : Stereo-Frames pro Half-Buffer  (z.B. 1024)
 *   NDSP_BUFCOUNT  : Anzahl Wave-Buffer              (Standard 4)
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

/** audio_get_pd_peak - Groesster PD-Ausschlag (0..32767) links/rechts seit
 *  dem letzten Aufruf (Diagnose: 0 = PD erzeugt keinen Ton). */
void audio_get_pd_peak(AudioCtx *ctx, int *l, int *r);

/** audio_get_underruns - Wie oft die Audio-Warteschlange leergelaufen ist
 *  (jeder Aussetzer = hoerbares Knacken). Zaehlt seit Programmstart. */
unsigned audio_get_underruns(const AudioCtx *ctx);

/** audio_get_dsp_load - PD-Rechenzeit in Prozent der Echtzeit (geglaettet).
 *  Ueber 100 = PD schafft es nicht mehr, Ton bricht ab. */
int audio_get_dsp_load(const AudioCtx *ctx);

/** audio_reset_dsp_load - Lastanzeige auf 0 setzen (z. B. nach Patchwechsel). */
void audio_reset_dsp_load(AudioCtx *ctx);

/** audio_deinit - NDSP stoppen, alle Ressourcen freigeben. */
void audio_deinit(AudioCtx *ctx);

/** audio_is_running - true solange NDSP aktiv. */
bool audio_is_running(const AudioCtx *ctx);
