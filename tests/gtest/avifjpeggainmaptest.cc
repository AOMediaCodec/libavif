// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <math.h>

#include "avif/avif.h"
#include "avifjpeg.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

//------------------------------------------------------------------------------

void CheckGainMapMetadata(
    const avifGainMap& gm, std::array<double, 3> gain_map_min,
    std::array<double, 3> gain_map_max, std::array<double, 3> gain_map_gamma,
    std::array<double, 3> base_offset, std::array<double, 3> alternate_offset,
    double base_hdr_headroom, double alternate_hdr_headroom) {
  const double kEpsilon = 1e-8;

  EXPECT_NEAR(static_cast<double>(gm.gainMapMinN[0]) / gm.gainMapMinD[0],
              gain_map_min[0], kEpsilon);
  EXPECT_NEAR(static_cast<double>(gm.gainMapMinN[1]) / gm.gainMapMinD[1],
              gain_map_min[1], kEpsilon);
  EXPECT_NEAR(static_cast<double>(gm.gainMapMinN[2]) / gm.gainMapMinD[2],
              gain_map_min[2], kEpsilon);

  EXPECT_NEAR(static_cast<double>(gm.gainMapMaxN[0]) / gm.gainMapMaxD[0],
              gain_map_max[0], kEpsilon);
  EXPECT_NEAR(static_cast<double>(gm.gainMapMaxN[1]) / gm.gainMapMaxD[1],
              gain_map_max[1], kEpsilon);
  EXPECT_NEAR(static_cast<double>(gm.gainMapMaxN[2]) / gm.gainMapMaxD[2],
              gain_map_max[2], kEpsilon);

  EXPECT_NEAR(static_cast<double>(gm.gainMapGammaN[0]) / gm.gainMapGammaD[0],
              gain_map_gamma[0], kEpsilon);
  EXPECT_NEAR(static_cast<double>(gm.gainMapGammaN[1]) / gm.gainMapGammaD[1],
              gain_map_gamma[1], kEpsilon);
  EXPECT_NEAR(static_cast<double>(gm.gainMapGammaN[2]) / gm.gainMapGammaD[2],
              gain_map_gamma[2], kEpsilon);

  EXPECT_NEAR(static_cast<double>(gm.baseOffsetN[0]) / gm.baseOffsetD[0],
              base_offset[0], kEpsilon);
  EXPECT_NEAR(static_cast<double>(gm.baseOffsetN[1]) / gm.baseOffsetD[1],
              base_offset[1], kEpsilon);
  EXPECT_NEAR(static_cast<double>(gm.baseOffsetN[2]) / gm.baseOffsetD[2],
              base_offset[2], kEpsilon);

  EXPECT_NEAR(
      static_cast<double>(gm.alternateOffsetN[0]) / gm.alternateOffsetD[0],
      alternate_offset[0], kEpsilon);
  EXPECT_NEAR(
      static_cast<double>(gm.alternateOffsetN[1]) / gm.alternateOffsetD[1],
      alternate_offset[1], kEpsilon);
  EXPECT_NEAR(
      static_cast<double>(gm.alternateOffsetN[2]) / gm.alternateOffsetD[2],
      alternate_offset[2], kEpsilon);

  EXPECT_NEAR(static_cast<double>(gm.baseHdrHeadroomN) / gm.baseHdrHeadroomD,
              base_hdr_headroom, kEpsilon);
  EXPECT_NEAR(
      static_cast<double>(gm.alternateHdrHeadroomN) / gm.alternateHdrHeadroomD,
      alternate_hdr_headroom, kEpsilon);
}

TEST(JpegTest, ReadJpegWithGainMap) {
  for (const char* filename : {"paris_exif_xmp_gainmap_bigendian.jpg",
                               "paris_exif_xmp_gainmap_littleendian.jpg"}) {
    SCOPED_TRACE(filename);

    const ImagePtr image =
        testutil::ReadImage(data_path, filename, AVIF_PIXEL_FORMAT_YUV444, 8,
                            AVIF_CHROMA_DOWNSAMPLING_AUTOMATIC,
                            /*ignore_icc=*/false, /*ignore_exif=*/false,
                            /*ignore_xmp=*/true, /*allow_changing_cicp=*/true,
                            /*ignore_gain_map=*/false);
    ASSERT_NE(image, nullptr);
    ASSERT_NE(image->gainMap, nullptr);
    ASSERT_NE(image->gainMap->image, nullptr);
    EXPECT_EQ(image->gainMap->image->width, 512u);
    EXPECT_EQ(image->gainMap->image->height, 384u);
    // Since ignore_xmp is true, there should be no XMP, even if it had to
    // be read to parse the gain map.
    EXPECT_EQ(image->xmp.size, 0u);

    CheckGainMapMetadata(*image->gainMap,
                         /*gain_map_min=*/{0.0, 0.0, 0.0},
                         /*gain_map_max=*/{3.5, 3.6, 3.7},
                         /*gain_map_gamma=*/{1.0, 1.0, 1.0},
                         /*base_offset=*/{0.0, 0.0, 0.0},
                         /*alternate_offset=*/{0.0, 0.0, 0.0},
                         /*base_hdr_headroom=*/0.0,
                         /*alternate_hdr_headroom=*/3.5);
  }
}

TEST(JpegTest, IgnoreGainMap) {
  const ImagePtr image = testutil::ReadImage(
      data_path, "paris_exif_xmp_gainmap_littleendian.jpg",
      AVIF_PIXEL_FORMAT_YUV444, 8, AVIF_CHROMA_DOWNSAMPLING_AUTOMATIC,
      /*ignore_icc=*/false, /*ignore_exif=*/false,
      /*ignore_xmp=*/false, /*allow_changing_cicp=*/true,
      /*ignore_gain_map=*/true);
  ASSERT_NE(image, nullptr);
  ASSERT_EQ(image->gainMap, nullptr);
  // Check there is xmp since ignore_xmp is false (just making sure that
  // ignore_gain_map=true has no impact on this).
  EXPECT_GT(image->xmp.size, 0u);
}

TEST(JpegTest, ParseXMP) {
  const std::string xmp = R"(
<?xpacket begin="" id="W5M0MpCehiHzreSzNTczkc9d"?>
<x:xmpmeta xmlns:x="adobe:ns:meta/">
  <foo:myelement> <!--  7.3 "Other XMP elements may appear around the rdf:RDF element." -->
    <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
      <rdf:Description xmlns:hdrgm="http://ns.adobe.com/hdr-gain-map/1.0/"
      hdrgm:Version="1.0"
      hdrgm:BaseRenditionIsHDR="True"
      hdrgm:OffsetSDR="0.046983"
      hdrgm:OffsetHDR="0.046983"
      hdrgm:HDRCapacityMin="0"
      hdrgm:HDRCapacityMax="3.9">
      <hdrgm:GainMapMin>
        <rdf:Seq>
        <rdf:li>0.025869</rdf:li>
        <rdf:li>0.075191</rdf:li>
        <rdf:li>0.142298</rdf:li>
        </rdf:Seq>
      </hdrgm:GainMapMin>
      <hdrgm:GainMapMax>
        <rdf:Seq>
        <rdf:li>3.527605</rdf:li>
        <rdf:li>2.830234</rdf:li>
        <!-- should work even with some whitespace -->
        <rdf:li>
          1.537243
        </rdf:li>
        </rdf:Seq>
      </hdrgm:GainMapMax>
      <hdrgm:Gamma>
        <rdf:Seq>
        <rdf:li>0.506828</rdf:li>
        <rdf:li>0.590032</rdf:li>
        <rdf:li>1.517708</rdf:li>
        </rdf:Seq>
      </hdrgm:Gamma>
      </rdf:Description>
    </rdf:RDF>
  </foo:myelement>
</x:xmpmeta>
<?xpacket end="w"?>
  )";
  GainMapPtr gainMap(avifGainMapCreate());
  ASSERT_TRUE(avifJPEGParseGainMapXMP((const uint8_t*)xmp.data(), xmp.size(),
                                      gainMap.get()));

  CheckGainMapMetadata(*gainMap,
                       /*gain_map_min=*/{0.025869, 0.075191, 0.142298},
                       /*gain_map_max=*/{3.527605, 2.830234, 1.537243},
                       /*gain_map_gamma=*/{0.506828, 0.590032, 1.517708},
                       /*base_offset=*/{0.046983, 0.046983, 0.046983},
                       /*alternate_offset=*/{0.046983, 0.046983, 0.046983},
                       /*base_hdr_headroom=*/3.9,
                       /*alternate_hdr_headroom=*/0.0);
}

TEST(JpegTest, ParseXMPAllDefaultValues) {
  const std::string xmp = R"(
<x:xmpmeta xmlns:x="adobe:ns:meta/">
  <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
    <rdf:Description rdf:about="stuff"
      xmlns:hdrgm="http://ns.adobe.com/hdr-gain-map/1.0/" hdrgm:Version="1.0">
    </rdf:Description>
  </rdf:RDF>
</x:xmpmeta>
<?xpacket end="w"?>
  )";
  GainMapPtr gainMap(avifGainMapCreate());
  ASSERT_TRUE(avifJPEGParseGainMapXMP((const uint8_t*)xmp.data(), xmp.size(),
                                      gainMap.get()));

  CheckGainMapMetadata(
      *gainMap,
      /*gain_map_min=*/{0.0, 0.0, 0.0},
      /*gain_map_max=*/{1.0, 1.0, 1.0},
      /*gain_map_gamma=*/{1.0, 1.0, 1.0},
      /*base_offset=*/{1.0 / 64.0, 1.0 / 64.0, 1.0 / 64.0},
      /*alternate_offset=*/{1.0 / 64.0, 1.0 / 64.0, 1.0 / 64.0},
      /*base_hdr_headroom=*/0.0,
      /*alternate_hdr_headroom=*/1.0);
}

TEST(JpegTest, ExtendedXmp) {
  const std::string xmp = R"(
<?xpacket begin="" id="W5M0MpCehiHzreSzNTczkc9d"?>
<x:xmpmeta xmlns:x="adobe:ns:meta/">
  <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
    <rdf:Description rdf:about="stuff"
      xmlns:hdrgm="http://ns.adobe.com/hdr-gain-map/1.0/" hdrgm:Version="1.0"
      hdrgm:BaseRenditionIsHDR="False"
      hdrgm:HDRCapacityMin="0"
      hdrgm:HDRCapacityMax="3.9">
    </rdf:Description>
  </rdf:RDF>
</x:xmpmeta>
<?xpacket end="w"?>

<x:xmpmeta xmlns:x="adobe:ns:meta/">
  <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
    <!-- Imagine this is some extended xmp that avifenc concatenated to
      the main XMP. As a result we have invalid XMP but should still be
      able to parse it. -->
    <stuff></stuff>
  </rdf:RDF>
</x:xmpmeta>
  )";
  GainMapPtr gainMap(avifGainMapCreate());
  ASSERT_TRUE(avifJPEGParseGainMapXMP((const uint8_t*)xmp.data(), xmp.size(),
                                      gainMap.get()));

  // Note that this test passes because the gain map metadata is in the primary
  // XMP. If it was in the extended part, we wouldn't detect it (but probably
  // should).
  CheckGainMapMetadata(
      *gainMap,
      /*gain_map_min=*/{0.0, 0.0, 0.0},
      /*gain_map_max=*/{1.0, 1.0, 1.0},
      /*gain_map_gamma=*/{1.0, 1.0, 1.0},
      /*base_offset=*/{1.0 / 64.0, 1.0 / 64.0, 1.0 / 64.0},
      /*alternate_offset=*/{1.0 / 64.0, 1.0 / 64.0, 1.0 / 64.0},
      /*base_hdr_headroom=*/0.0,
      /*alternate_hdr_headroom=*/3.9);
}

TEST(JpegTest, InvalidNumberOfValues) {
  const std::string xmp = R"(
<x:xmpmeta xmlns:x="adobe:ns:meta/">
  <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
    <rdf:Description xmlns:hdrgm="http://ns.adobe.com/hdr-gain-map/1.0/"
    hdrgm:Version="1.0"
    hdrgm:BaseRenditionIsHDR="False"
    hdrgm:OffsetSDR="0.046983"
    hdrgm:OffsetHDR="0.046983"
    hdrgm:HDRCapacityMin="0"
    hdrgm:HDRCapacityMax="3.9">
    <hdrgm:GainMapMin>
      <rdf:Seq><!--invalid! only two values-->
      <rdf:li>0.023869</rdf:li>
      <rdf:li>0.075191</rdf:li>
      </rdf:Seq>
    </hdrgm:GainMapMin>
  </rdf:RDF>
</x:xmpmeta>
  )";
  GainMapPtr gainMap(avifGainMapCreate());
  EXPECT_FALSE(avifJPEGParseGainMapXMP((const uint8_t*)xmp.data(), xmp.size(),
                                       gainMap.get()));
}

TEST(JpegTest, WrongVersion) {
  const std::string xmp = R"(
<x:xmpmeta xmlns:x="adobe:ns:meta/">
  <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
    <rdf:Description rdf:about=""
      xmlns:hdrgm="http://ns.adobe.com/hdr-gain-map/1.0/" hdrgm:Version="2.0">
    </rdf:Description>
  </rdf:RDF>
</x:xmpmeta>
  )";
  GainMapPtr gainMap(avifGainMapCreate());
  EXPECT_FALSE(avifJPEGParseGainMapXMP((const uint8_t*)xmp.data(), xmp.size(),
                                       gainMap.get()));
}

TEST(JpegTest, InvalidXMP) {
  const std::string xmp = R"(
<x:xmpmeta xmlns:x="adobe:ns:meta/">
    <rdf:Description rdf:about=""
      xmlns:hdrgm="http://ns.adobe.com/hdr-gain-map/1.0/" hdrgm:Version="2.0">
    </rdf:Description>
  </rdf:RDF>
</x:xmpmeta>
  )";
  GainMapPtr gainMap(avifGainMapCreate());
  EXPECT_FALSE(avifJPEGParseGainMapXMP((const uint8_t*)xmp.data(), xmp.size(),
                                       gainMap.get()));
}

TEST(JpegTest, EmptyXMP) {
  const std::string xmp = "";
  GainMapPtr gainMap(avifGainMapCreate());
  EXPECT_FALSE(avifJPEGParseGainMapXMP((const uint8_t*)xmp.data(), xmp.size(),
                                       gainMap.get()));
}

//------------------------------------------------------------------------------

}  // namespace
}  // namespace avif

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  if (argc != 2) {
    std::cerr << "There must be exactly one argument containing the path to "
                 "the test data folder"
              << std::endl;
    return 1;
  }
  avif::data_path = argv[1];
  return RUN_ALL_TESTS();
}
