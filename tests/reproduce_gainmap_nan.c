/*
 * reproduce_gainmap_nan.c — Demonstrates NaN crash in gain map tone mapping.
 *
 * Without the fminf/fmaxf fix, this triggers an assertion failure
 * in avifSetRGBAPixel() (debug builds) or undefined float-to-int
 * conversion (release builds).
 *
 * The NaN arises from IEEE 754 indeterminate form 0 * Inf:
 *   - baseOffset = 0, so (baseLinear + baseOffset) = 0 for a black pixel
 *   - gainMapMax = 1000, so exp2f(lerp(0, 1000, 1.0) * 1.0) = +Inf
 *   - 0.0f * +Inf = NaN
 *   - AVIF_CLAMP(NaN, 0, 1) = NaN (ternary comparisons with NaN are false)
 *
 * Build (from libavif root):
 *
 *   mkdir build && cd build
 *   cmake .. -DAVIF_CODEC_AOM=LOCAL -DAVIF_LIBYUV=LOCAL \
 *     -DCMAKE_BUILD_TYPE=Debug -DBUILD_SHARED_LIBS=OFF
 *   cmake --build . --target avif -j$(nproc)
 *   cd ..
 *
 *   cc -g -O1 -I include tests/reproduce_gainmap_nan.c \
 *     build/libavif.a build/_deps/libyuv-build/libyuv.a \
 *     build/_deps/aom-build/libaom.a -lstdc++ -lm -lpthread \
 *     -o reproduce_gainmap_nan
 *
 *   ./reproduce_gainmap_nan
 *
 * Expected without fix: assertion failure in avifSetRGBAPixel
 * Expected with fix:    "PASS: no crash"
 */

#include <avif/avif.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    /* 2x2 black base image (sRGB, BT.709) */
    avifImage *base = avifImageCreate(2, 2, 8, AVIF_PIXEL_FORMAT_YUV444);
    if (!base) {
        fprintf(stderr, "Failed to create base image\n");
        return 1;
    }
    base->colorPrimaries = AVIF_COLOR_PRIMARIES_SRGB;
    base->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_SRGB;
    base->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT709;
    base->yuvRange = AVIF_RANGE_FULL;
    if (avifImageAllocatePlanes(base, AVIF_PLANES_YUV) != AVIF_RESULT_OK) {
        fprintf(stderr, "Failed to allocate base planes\n");
        avifImageDestroy(base);
        return 1;
    }
    /* Y=0 (black), U=128/V=128 (neutral chroma) */
    memset(base->yuvPlanes[0], 0, (size_t)base->yuvRowBytes[0] * 2);
    memset(base->yuvPlanes[1], 128, (size_t)base->yuvRowBytes[1] * 2);
    memset(base->yuvPlanes[2], 128, (size_t)base->yuvRowBytes[2] * 2);

    /* 2x2 gain map image — all pixels at maximum (255 -> 1.0 normalized) */
    avifGainMap *gainMap = avifGainMapCreate();
    if (!gainMap) {
        fprintf(stderr, "Failed to create gain map\n");
        avifImageDestroy(base);
        return 1;
    }
    gainMap->image = avifImageCreate(2, 2, 8, AVIF_PIXEL_FORMAT_YUV444);
    if (!gainMap->image) {
        fprintf(stderr, "Failed to create gain map image\n");
        avifGainMapDestroy(gainMap);
        avifImageDestroy(base);
        return 1;
    }
    gainMap->image->yuvRange = AVIF_RANGE_FULL;
    gainMap->image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY;
    if (avifImageAllocatePlanes(gainMap->image, AVIF_PLANES_YUV) != AVIF_RESULT_OK) {
        fprintf(stderr, "Failed to allocate gain map planes\n");
        avifGainMapDestroy(gainMap);
        avifImageDestroy(base);
        return 1;
    }
    memset(gainMap->image->yuvPlanes[0], 255, (size_t)gainMap->image->yuvRowBytes[0] * 2);
    memset(gainMap->image->yuvPlanes[1], 255, (size_t)gainMap->image->yuvRowBytes[1] * 2);
    memset(gainMap->image->yuvPlanes[2], 255, (size_t)gainMap->image->yuvRowBytes[2] * 2);

    /*
     * Gain map metadata crafted to trigger NaN:
     *   gainMapMin  = 0     -> lerp lower bound
     *   gainMapMax  = 1000  -> lerp upper bound
     *   gamma       = 1     -> no gamma distortion
     *   baseOffset  = 0     -> (baseLinear + 0) = 0 for black pixels
     *   altOffset   = 0
     *
     * The math: lerp(0, 1000, powf(1.0, 1.0)) = 1000
     *           exp2f(1000 * weight) = +Inf
     *           (0.0 + 0.0) * +Inf = NaN  (IEEE 754)
     */
    for (int c = 0; c < 3; ++c) {
        gainMap->gainMapMin[c] = (avifSignedFraction){ 0, 1 };
        gainMap->gainMapMax[c] = (avifSignedFraction){ 1000, 1 };
        gainMap->gainMapGamma[c] = (avifUnsignedFraction){ 1, 1 };
        gainMap->baseOffset[c] = (avifSignedFraction){ 0, 1 };
        gainMap->alternateOffset[c] = (avifSignedFraction){ 0, 1 };
    }
    gainMap->baseHdrHeadroom = (avifUnsignedFraction){ 0, 1 };
    gainMap->alternateHdrHeadroom = (avifUnsignedFraction){ 6, 1 };
    gainMap->useBaseColorSpace = 1;
    gainMap->altColorPrimaries = AVIF_COLOR_PRIMARIES_SRGB;
    gainMap->altTransferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_SRGB;
    gainMap->altMatrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT709;
    gainMap->altYUVRange = AVIF_RANGE_FULL;
    gainMap->altDepth = 8;
    gainMap->altPlaneCount = 3;

    /* Output tone-mapped image — set format/depth only.
     * avifRGBImageApplyGainMap sets width/height and allocates pixels internally. */
    avifRGBImage toneMap;
    memset(&toneMap, 0, sizeof(toneMap));
    toneMap.depth = 8;
    toneMap.format = AVIF_RGB_FORMAT_RGBA;

    avifContentLightLevelInformationBox clli;
    memset(&clli, 0, sizeof(clli));
    avifDiagnostics diag;
    avifDiagnosticsClearError(&diag);

    /* Apply with full HDR headroom (weight = 1.0) */
    avifResult result = avifImageApplyGainMap(base, gainMap, 6.0f,
                                              AVIF_COLOR_PRIMARIES_SRGB,
                                              AVIF_TRANSFER_CHARACTERISTICS_SRGB,
                                              &toneMap, &clli, &diag);

    if (result == AVIF_RESULT_OK) {
        printf("Result: OK\n");
    } else {
        printf("Result: %s (%s)\n", avifResultToString(result), diag.error);
    }
    printf("PASS: no crash\n");

    avifRGBImageFreePixels(&toneMap);
    avifGainMapDestroy(gainMap);
    avifImageDestroy(base);
    return 0;
}
