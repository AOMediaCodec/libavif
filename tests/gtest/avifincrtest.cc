// Copyright 2022 Google LLC. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include <fstream>
#include <iostream>
#include <string>
#include <tuple>

#include "avifincrtest_helpers.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

using testing::Bool;
using testing::Combine;
using testing::Values;

namespace libavif
{
namespace
{

//------------------------------------------------------------------------------

// Used to pass the data folder path to the GoogleTest suites.
const char * dataPath = nullptr;

// Reads the file with fileName into bytes and returns them.
testutil::avifRWDataCleaner readFile(const char * fileName)
{
    std::ifstream file(std::string(dataPath) + "/" + fileName, std::ios::binary | std::ios::ate);
    testutil::avifRWDataCleaner bytes;
    avifRWDataRealloc(&bytes, file.good() ? static_cast<size_t>(file.tellg()) : 0);
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char *>(bytes.data), static_cast<std::streamsize>(bytes.size));
    return bytes;
}

//------------------------------------------------------------------------------

// Check that non-incremental and incremental decodings of a grid AVIF produce the same pixels.
TEST(IncrementalTest, Decode)
{
    const testutil::avifRWDataCleaner encodedAvif = readFile("sofa_grid1x5_420.avif");
    ASSERT_NE(encodedAvif.size, 0u);
    testutil::avifImagePtr reference(avifImageCreateEmpty(), avifImageDestroy);
    ASSERT_NE(reference, nullptr);
    testutil::avifDecoderPtr decoder(avifDecoderCreate(), avifDecoderDestroy);
    ASSERT_NE(decoder, nullptr);
    ASSERT_EQ(avifDecoderReadMemory(decoder.get(), reference.get(), encodedAvif.data, encodedAvif.size), AVIF_RESULT_OK);

    // Cell height is hardcoded because there is no API to extract it from an encoded payload.
    testutil::decodeIncrementally(encodedAvif, /*isPersistent=*/true, /*giveSizeHint=*/true, /*useNthImageApi=*/false, *reference, /*cellHeight=*/154);
}

//------------------------------------------------------------------------------

class IncrementalTest : public testing::TestWithParam<std::tuple</*width=*/uint32_t,
                                                                 /*height=*/uint32_t,
                                                                 /*createAlpha=*/bool,
                                                                 /*flatCells=*/bool,
                                                                 /*encodedAvifIsPersistent=*/bool,
                                                                 /*giveSizeHint=*/bool,
                                                                 /*useNthImageApi=*/bool>>
{
};

// Encodes then decodes a window of width*height pixels at the middle of the image.
// Check that non-incremental and incremental decodings produce the same pixels.
TEST_P(IncrementalTest, EncodeDecode)
{
    const uint32_t width = std::get<0>(GetParam());
    const uint32_t height = std::get<1>(GetParam());
    const bool createAlpha = std::get<2>(GetParam());
    const bool flatCells = std::get<3>(GetParam());
    const bool encodedAvifIsPersistent = std::get<4>(GetParam());
    const bool giveSizeHint = std::get<5>(GetParam());
    const bool useNthImageApi = std::get<6>(GetParam());

    // Load an image. It does not matter that it comes from an AVIF file.
    testutil::avifImagePtr image(avifImageCreateEmpty(), avifImageDestroy);
    ASSERT_NE(image, nullptr);
    const testutil::avifRWDataCleaner imageBytes = readFile("sofa_grid1x5_420.avif");
    ASSERT_NE(imageBytes.size, 0u);
    testutil::avifDecoderPtr decoder(avifDecoderCreate(), avifDecoderDestroy);
    ASSERT_NE(decoder, nullptr);
    ASSERT_EQ(avifDecoderReadMemory(decoder.get(), image.get(), imageBytes.data, imageBytes.size), AVIF_RESULT_OK);

    // Encode then decode it.
    testutil::avifRWDataCleaner encodedAvif;
    uint32_t cellWidth, cellHeight;
    testutil::encodeRectAsIncremental(*image, width, height, createAlpha, flatCells, &encodedAvif, &cellWidth, &cellHeight);
    testutil::decodeNonIncrementallyAndIncrementally(encodedAvif, encodedAvifIsPersistent, giveSizeHint, useNthImageApi, cellHeight);
}

INSTANTIATE_TEST_SUITE_P(WholeImage,
                         IncrementalTest,
                         Combine(/*width=*/Values(1024),
                                 /*height=*/Values(770),
                                 /*createAlpha=*/Values(true),
                                 /*flatCells=*/Bool(),
                                 /*encodedAvifIsPersistent=*/Values(true),
                                 /*giveSizeHint=*/Values(true),
                                 /*useNthImageApi=*/Values(false)));

// avifEncoderAddImageInternal() only accepts grids of one unique cell, or grids where width and height are both at least 64.
INSTANTIATE_TEST_SUITE_P(SingleCell,
                         IncrementalTest,
                         Combine(/*width=*/Values(1),
                                 /*height=*/Values(1),
                                 /*createAlpha=*/Bool(),
                                 /*flatCells=*/Bool(),
                                 /*encodedAvifIsPersistent=*/Bool(),
                                 /*giveSizeHint=*/Bool(),
                                 /*useNthImageApi=*/Bool()));

// Chroma subsampling requires even dimensions. See ISO 23000-22 section 7.3.11.4.2.
INSTANTIATE_TEST_SUITE_P(SinglePixel,
                         IncrementalTest,
                         Combine(/*width=*/Values(64, 66),
                                 /*height=*/Values(64, 66),
                                 /*createAlpha=*/Bool(),
                                 /*flatCells=*/Bool(),
                                 /*encodedAvifIsPersistent=*/Bool(),
                                 /*giveSizeHint=*/Bool(),
                                 /*useNthImageApi=*/Bool()));

//------------------------------------------------------------------------------

} // namespace
} // namespace libavif

int main(int argc, char ** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    if (argc < 2) {
        std::cerr << "The path to the test data folder must be provided as an argument" << std::endl;
        return 1;
    }
    libavif::dataPath = argv[1];
    return RUN_ALL_TESTS();
}
