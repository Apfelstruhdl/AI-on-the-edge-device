// Host-side unit tests for FrameConsistency (no ESP-IDF needed).
//
//   g++ -std=c++11 -Wall -Wextra -I ../../components/jomjol_image_proc
//       test_frame_consistency_host.cpp ../../components/jomjol_image_proc/FrameConsistency.cpp -o frame_test
//   ./frame_test
//
// Optional: score real frame pairs (needs the stb submodule). Build with
//   -DFRAMECHECK_REAL_IMAGES -I ../../components/stb
// and run `./frame_test list.txt`, where list.txt holds one JPEG path per line in capture order; every
// consecutive pair is scored and pairs above the threshold are printed.
#include "FrameConsistency.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>

static int failures = 0, passes = 0;

static void check(bool ok, const char *name, float value)
{
    printf("%s  %-52s band change %.3f\n", ok ? "PASS" : "FAIL", name, value);
    ok ? passes++ : failures++;
}

static const int W = 640, H = 480, C = 3;

// A structured test scene: smooth gradients plus a fixed pseudo-random 4x4-px texture, so that a
// shifted band really changes block means (a flat image would hide a shift).
static std::vector<uint8_t> scene()
{
    std::vector<uint8_t> img((size_t)W * H * C);
    uint32_t s = 12345;
    std::vector<uint8_t> tex((W / 4) * (H / 4));
    for (auto &t : tex) { s = s * 1103515245u + 12345u; t = (uint8_t)((s >> 16) & 0x7f); }
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            int v = 40 + (x * 60) / W + (y * 40) / H + tex[(y / 4) * (W / 4) + x / 4];
            uint8_t *p = &img[((size_t)y * W + x) * C];
            p[0] = (uint8_t)v; p[1] = (uint8_t)(v * 9 / 10); p[2] = (uint8_t)(v * 8 / 10);
        }
    return img;
}

static float band(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b)
{
    return FrameBandChange(MakeFrameFingerprint(a.data(), W, H, C), MakeFrameFingerprint(b.data(), W, H, C));
}

static void fillWedge(std::vector<uint8_t> &img, int cx, int cy, double angle)
{
    // a red pointer ~35 px long, ~12 px wide, like an analog dial pointer
    for (int y = cy - 40; y <= cy + 40; ++y)
        for (int x = cx - 40; x <= cx + 40; ++x) {
            double dx = x - cx, dy = y - cy, r = sqrt(dx * dx + dy * dy);
            if (r > 35) continue;
            double along = dx * cos(angle) + dy * sin(angle), across = -dx * sin(angle) + dy * cos(angle);
            if (along > 0 && fabs(across) < 6.0 * (1.0 - along / 40.0)) {
                uint8_t *p = &img[((size_t)y * W + x) * C];
                p[0] = 220; p[1] = 40; p[2] = 40;
            }
        }
}

static void synthetic()
{
    const std::vector<uint8_t> base = scene();

    check(band(base, base) == 0.0f, "identical frames", band(base, base));

    {   // sensor noise +-3 grey levels on every pixel
        std::vector<uint8_t> n = base; uint32_t s = 7;
        for (auto &v : n) { s = s * 1103515245u + 12345u; int d = (int)((s >> 16) % 7) - 3; int x = v + d; v = (uint8_t)(x < 0 ? 0 : x > 255 ? 255 : x); }
        float m = band(base, n); check(FramesConsistent(MakeFrameFingerprint(base.data(), W, H, C), MakeFrameFingerprint(n.data(), W, H, C)), "sensor noise is consistent", m);
    }
    {   // all four analog pointers turn a quarter revolution (far more than in the ~0.1 s between captures)
        std::vector<uint8_t> a = base, b = base;
        const int pos[4][2] = {{465, 296}, {412, 391}, {322, 428}, {203, 383}};
        for (auto &p : pos) { fillWedge(a, p[0], p[1], 0.3); fillWedge(b, p[0], p[1], 0.3 + 1.57); }
        float m = band(a, b); check(m <= FRAME_CONSISTENCY_MAX_BAND_CHANGE, "four pointers turning is consistent", m);
    }
    {   // small global brightness change (auto exposure settling)
        std::vector<uint8_t> b = base; for (auto &v : b) v = (uint8_t)(v + 5 > 255 ? 255 : v + 5);
        float m = band(base, b); check(m <= FRAME_CONSISTENCY_MAX_BAND_CHANGE, "brightness +5 is consistent", m);
    }
    {   // torn frame: the lower part shifted sideways by 40 px (seen 2026-10-05 23:54)
        std::vector<uint8_t> b = base;
        for (int y = 300; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                int sx = x - 40 < 0 ? 0 : x - 40;
                for (int c = 0; c < C; ++c) b[((size_t)y * W + x) * C + c] = base[((size_t)y * W + sx) * C + c];
            }
        float m = band(base, b); check(m > FRAME_CONSISTENCY_MAX_BAND_CHANGE, "torn frame (lower part shifted 40 px) detected", m);
    }
    {   // truncated frame: everything below row 320 black (seen 2026-10-01 00:33)
        std::vector<uint8_t> b = base;
        for (size_t i = (size_t)320 * W * C; i < b.size(); ++i) b[i] = 0;
        float m = band(base, b); check(m > FRAME_CONSISTENCY_MAX_BAND_CHANGE, "truncated frame (bottom third black) detected", m);
    }
    {   // dark band across the top (seen 2026-10-05 06:18 and 2026-10-07 08:40)
        std::vector<uint8_t> b = base;
        for (size_t i = 0; i < (size_t)60 * W * C; ++i) b[i] = (uint8_t)(b[i] * 6 / 10);
        float m = band(base, b); check(m > FRAME_CONSISTENCY_MAX_BAND_CHANGE, "dark band across the top detected", m);
    }
    {   // a shift of a single narrow band of 16 rows by 24 px
        std::vector<uint8_t> b = base;
        for (int y = 200; y < 216; ++y)
            for (int x = 0; x < W; ++x) {
                int sx = x + 24 >= W ? W - 1 : x + 24;
                for (int c = 0; c < C; ++c) b[((size_t)y * W + x) * C + c] = base[((size_t)y * W + sx) * C + c];
            }
        float m = band(base, b); check(m > FRAME_CONSISTENCY_MAX_BAND_CHANGE, "narrow 16-row band shifted 24 px detected", m);
    }
    {   // grey (single-channel) input works the same way
        std::vector<uint8_t> g((size_t)W * H, 100), g2 = g;
        for (size_t i = (size_t)400 * W; i < g2.size(); ++i) g2[i] = 0;
        float m = FrameBandChange(MakeFrameFingerprint(g.data(), W, H, 1), MakeFrameFingerprint(g2.data(), W, H, 1));
        check(m > FRAME_CONSISTENCY_MAX_BAND_CHANGE, "single-channel truncation detected", m);
    }
    {   // fingerprints of different sizes are never treated as consistent
        float m = FrameBandChange(MakeFrameFingerprint(base.data(), W, H, C), MakeFrameFingerprint(base.data(), 320, 240, C));
        check(m == 1.0f, "different sizes are inconsistent", m);
    }
    {   // invalid input yields an empty fingerprint, which is inconsistent with anything
        FrameFingerprint e = MakeFrameFingerprint(NULL, W, H, C);
        float m = FrameBandChange(e, e);
        check(e.blocks.empty() && m == 1.0f, "NULL image gives an empty, inconsistent fingerprint", m);
    }
}

#ifdef FRAMECHECK_REAL_IMAGES
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <string>
#include <algorithm>

static int real(const char *listFile)
{
    FILE *f = fopen(listFile, "r");
    if (!f) { printf("cannot open %s\n", listFile); return 1; }
    char line[1024];
    std::vector<std::string> paths;
    while (fgets(line, sizeof line, f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (!s.empty()) paths.push_back(s);
    }
    fclose(f);

    FrameFingerprint prev; std::string prevPath; std::vector<float> all;
    for (const std::string &p : paths) {
        int w, h, c;
        unsigned char *img = stbi_load(p.c_str(), &w, &h, &c, 3);
        if (!img) { printf("decode failed: %s\n", p.c_str()); continue; }
        FrameFingerprint fp = MakeFrameFingerprint(img, w, h, 3);
        stbi_image_free(img);
        if (!prev.blocks.empty()) {
            float m = FrameBandChange(prev, fp);
            all.push_back(m);
            if (m > FRAME_CONSISTENCY_MAX_BAND_CHANGE) printf("INCONSISTENT %.3f  %s -> %s\n", m, prevPath.c_str(), p.c_str());
        }
        prev = fp; prevPath = p;
    }
    std::sort(all.begin(), all.end());
    if (!all.empty())
        printf("pairs %zu  p50 %.3f  p99 %.3f  p99.9 %.3f  max %.3f  above threshold %zu\n", all.size(),
               all[all.size() / 2], all[all.size() * 99 / 100], all[all.size() * 999 / 1000], all.back(),
               (size_t)std::count_if(all.begin(), all.end(), [](float v) { return v > FRAME_CONSISTENCY_MAX_BAND_CHANGE; }));
    return 0;
}
#endif

int main(int argc, char **argv)
{
#ifdef FRAMECHECK_REAL_IMAGES
    if (argc > 1) return real(argv[1]);
#else
    (void)argc; (void)argv;
#endif
    synthetic();
    printf("==== %d passed, %d failed ====\n", passes, failures);
    return failures == 0 ? 0 : 1;
}
