/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * oa2dp_audio_visualizer.h - WASAPI loopback capture + spectrum bands
 *
 * Captures whatever is playing through the system default render
 * endpoint (regardless of whether the user's headphones are
 * physically present) and exposes a small set of frequency-band
 * magnitudes for the UI to draw as a visualizer.
 */

#ifndef OA2DP_AUDIO_VISUALIZER_H
#define OA2DP_AUDIO_VISUALIZER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Number of frequency bands the visualizer reports.  Fixed at
 * compile time so the UI can stack-allocate. */
#define OA2DP_VIS_BANDS 16

/* Spawn the loopback capture worker.  Returns 0 on success. */
int  oa2dp_audio_visualizer_init(void);

/* Stop the worker, wait for it to exit, release WASAPI resources. */
void oa2dp_audio_visualizer_shutdown(void);

/* Copy the current band magnitudes (each in [0..1]) into 'out'.
 * 'count' must be <= OA2DP_VIS_BANDS.  Safe to call from the UI
 * thread; takes a brief lock to snapshot the worker's state. */
void oa2dp_audio_visualizer_get_bands(float *out, int count);

/*
 * Snapshot the most recent up-to-max_samples L/R sample pairs
 * from the WASAPI loopback ring buffer (in chronological order,
 * oldest first).  Used by the oscilloscope and vectorscope
 * visualizer modes.  Writes the actual count returned into
 * *out_count (will be <= max_samples and <= the ring buffer's
 * fill level).  Safe to call from the UI thread.
 */
void oa2dp_audio_visualizer_get_samples(float *out_l, float *out_r,
                                        int max_samples, int *out_count);

#ifdef __cplusplus
}
#endif

#endif /* OA2DP_AUDIO_VISUALIZER_H */
