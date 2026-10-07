#include "FrameConsistency.h"

#include <stdlib.h>

FrameFingerprint MakeFrameFingerprint(const uint8_t *image, int width, int height, int channels, int blockSize)
{
    FrameFingerprint fp;

    if ((image == NULL) || (width <= 0) || (height <= 0) || (channels <= 0) || (blockSize <= 0)) {
        return fp;
    }

    fp.cols = width / blockSize;
    fp.rows = height / blockSize;
    fp.blocks.assign(fp.cols * fp.rows, 0);

    const int pixelsPerBlock = blockSize * blockSize;

    for (int by = 0; by < fp.rows; ++by) {
        for (int bx = 0; bx < fp.cols; ++bx) {
            uint32_t sum = 0;

            for (int y = by * blockSize; y < (by + 1) * blockSize; ++y) {
                const uint8_t *p = image + ((size_t)y * width + (size_t)bx * blockSize) * channels;

                for (int x = 0; x < blockSize; ++x, p += channels) {
                    if (channels >= 3) {
                        sum += (77 * p[0] + 150 * p[1] + 29 * p[2]) >> 8; // ITU-R BT.601 luma
                    }
                    else {
                        sum += p[0];
                    }
                }
            }

            fp.blocks[by * fp.cols + bx] = (uint8_t)(sum / pixelsPerBlock);
        }
    }

    return fp;
}

float FrameBandChange(const FrameFingerprint &a, const FrameFingerprint &b, int blockThreshold)
{
    if ((a.cols != b.cols) || (a.rows != b.rows) || (a.cols == 0) || (a.rows == 0) ||
        (a.blocks.size() != b.blocks.size())) {
        return 1.0f;
    }

    int worst = 0;

    for (int by = 0; by < a.rows; ++by) {
        int changed = 0;

        for (int bx = 0; bx < a.cols; ++bx) {
            const int i = by * a.cols + bx;

            if (abs((int)a.blocks[i] - (int)b.blocks[i]) > blockThreshold) {
                changed++;
            }
        }

        if (changed > worst) {
            worst = changed;
        }
    }

    return (float)worst / (float)a.cols;
}
