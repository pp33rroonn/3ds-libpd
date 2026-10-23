#pragma once
/* camtune.h - live tuning parameters for the camera "Off" stabilization stage.
 *
 * These 5 globals are driven by the "Cam Tune" fader bank on the CTRL panel
 * (see ctrl_panel.c). They OVERRIDE the hard-coded Off-stage (Buffer FX index 0,
 * Luma-Lock) constants in cam_view.c whenever the camera is active, so the user
 * can dial in the stabilization live on hardware instead of rebuilding per
 * experiment. They do NOT touch the X1..X10 Buffer FX ladder behaviour - only
 * the Off stage reads them, and X10 raw passthrough stays bit-identical.
 *
 * All 5 values are normalized 0..1 internally (the fader range). Each cam
 * pipeline / camera consumer maps its own value to a hardware/algorithm range;
 * the mapping constants live next to the code that uses them.
 *
 * DEFAULT POSITIONS reproduce today's Off behaviour, so at the defaults
 * "nothing changes" vs. the current build:
 *   g_camtune_exposure = 0.00  -> Auto exposure (CAMU_SetAutoExposure(true)).
 *   g_camtune_hold     = 0.50  -> LUMA_LOCK_FREEZE 20 (release = freeze + 8 = 28).
 *   g_camtune_temporal = 0.00  -> no Off-stage EMA blend (today has none).
 *   g_camtune_spatial  = 0.50  -> dither hysteresis margin 24, 3x3 median OFF.
 *   g_camtune_sharpen  = 0.25  -> unsharp-mask strength 2 (today's light unsharp).
 *
 * Fader meaning (0..1):
 *   Exposure  0 = Auto; >0 = lock auto-exposure off + fixed exposure (brighter
 *             toward 1). The key lever against br "breathing". AWB stays ON.
 *   Hold      static-scene freeze threshold. 0 = never freeze (always live),
 *             higher = freezes more readily when the scene is still.
 *   Temporal  Off-stage EMA smoothing. 0 = none, higher = heavier temporal blend.
 *   Spatial   combined 3x3 median strength + dither hysteresis margin. 0 = off
 *             (sharpest), higher = stronger denoise + stickier dither.
 *   Sharpen   unsharp-mask strength. 0 = none, higher = crisper edges.
 */

#ifdef __cplusplus
extern "C" {
#endif

extern float g_camtune_exposure;   /* 0 = Auto, >0 = fixed exposure lock */
extern float g_camtune_hold;       /* static-scene freeze threshold      */
extern float g_camtune_temporal;   /* Off-stage EMA smoothing strength   */
extern float g_camtune_spatial;    /* 3x3 median + dither hysteresis      */
extern float g_camtune_sharpen;    /* unsharp-mask strength               */

#ifdef __cplusplus
}
#endif
