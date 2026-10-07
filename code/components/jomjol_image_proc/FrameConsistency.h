#pragma once

#ifndef FRAMECONSISTENCY_H
#define FRAMECONSISTENCY_H

#include <stdint.h>
#include <vector>

/**
 * Detect corrupted camera frames by comparing two captures of the same scene.
 *
 * A marginal camera connection occasionally delivers a frame that is torn (a horizontal band shifted
 * sideways), truncated (the rest of the frame black) or banded in brightness. Such a frame still
 * decodes as a valid JPEG and can be read as a plausible but wrong value. Two captures taken a
 * fraction of a second apart show the same scene, so a large disagreement between them means one of
 * them is corrupted.
 *
 * The comparison works on a coarse fingerprint (mean luminance per block), so it needs no second
 * full-size image buffer. It looks at horizontal bands of blocks: a turning pointer, a moving digit
 * wheel or sensor noise changes only a few blocks of a band, while the corruptions above change most
 * blocks of at least one band.
 */

struct FrameFingerprint {
    int cols = 0;                 // blocks per row
    int rows = 0;                 // block rows (bands)
    std::vector<uint8_t> blocks;  // rows * cols mean luminances, row-major
};

/** Mean luminance of each blockSize x blockSize block; a partial block at the right/bottom edge is ignored. */
FrameFingerprint MakeFrameFingerprint(const uint8_t *image, int width, int height, int channels, int blockSize = 8);

/**
 * Largest fraction, over all bands, of blocks whose mean luminance differs by more than blockThreshold.
 * Returns 1.0 if the fingerprints are not comparable (different or zero size).
 */
float FrameBandChange(const FrameFingerprint &a, const FrameFingerprint &b, int blockThreshold = 12);

/**
 * Measured on 8900 pairs of real frames one minute apart (including heavy flow): normal pairs never
 * exceeded 0.29, while every pair involving one of 8 corrupted frames scored 0.61 or more.
 */
#define FRAME_CONSISTENCY_MAX_BAND_CHANGE 0.45f

inline bool FramesConsistent(const FrameFingerprint &a, const FrameFingerprint &b)
{
    return FrameBandChange(a, b) <= FRAME_CONSISTENCY_MAX_BAND_CHANGE;
}

#endif // FRAMECONSISTENCY_H
