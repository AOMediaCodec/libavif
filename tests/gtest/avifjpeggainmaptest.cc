// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <math.h>

#include "avif/avif.h"
#include "avifjpeg.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace libavif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

//------------------------------------------------------------------------------

void CheckGainMapMetadata(const avifGainMapMetadata& m,
                          std::array<double, 3> gain_map_min,
                          std::array<double, 3> gain_map_max,
                          std::array<double, 3> gain_map_gamma,
                          std::array<double, 3> offset_sdr,
                          std::array<double, 3> offset_hdr,
                          double hdr_capacity_min, double hdr_capacity_max,
                          bool base_rendition_is_hdr) {
  const double kEpsilon = 1e-8;

  EXPECT_NEAR(static_cast<double>(m.gainMapMinN[0]) / m.gainMapMinD[0],
              gain_map_min[0], kEpsilon);
  EXPECT_NEAR(static_cast<double>(m.gainMapMinN[1]) / m.gainMapMinD[1],
              gain_map_min[1], kEpsilon);
  EXPECT_NEAR(static_cast<double>(m.gainMapMinN[2]) / m.gainMapMinD[2],
              gain_map_min[2], kEpsilon);

  EXPECT_NEAR(static_cast<double>(m.gainMapMaxN[0]) / m.gainMapMaxD[0],
              gain_map_max[0], kEpsilon);
  EXPECT_NEAR(static_cast<double>(m.gainMapMaxN[1]) / m.gainMapMaxD[1],
              gain_map_max[1], kEpsilon);
  EXPECT_NEAR(static_cast<double>(m.gainMapMaxN[2]) / m.gainMapMaxD[2],
              gain_map_max[2], kEpsilon);

  EXPECT_NEAR(static_cast<double>(m.gainMapGammaN[0]) / m.gainMapGammaD[0],
              gain_map_gamma[0], kEpsilon);
  EXPECT_NEAR(static_cast<double>(m.gainMapGammaN[1]) / m.gainMapGammaD[1],
              gain_map_gamma[1], kEpsilon);
  EXPECT_NEAR(static_cast<double>(m.gainMapGammaN[2]) / m.gainMapGammaD[2],
              gain_map_gamma[2], kEpsilon);

  EXPECT_NEAR(static_cast<double>(m.offsetSdrN[0]) / m.offsetSdrD[0],
              offset_sdr[0], kEpsilon);
  EXPECT_NEAR(static_cast<double>(m.offsetSdrN[1]) / m.offsetSdrD[1],
              offset_sdr[1], kEpsilon);
  EXPECT_NEAR(static_cast<double>(m.offsetSdrN[2]) / m.offsetSdrD[2],
              offset_sdr[2], kEpsilon);

  EXPECT_NEAR(static_cast<double>(m.offsetHdrN[0]) / m.offsetHdrD[0],
              offset_hdr[0], kEpsilon);
  EXPECT_NEAR(static_cast<double>(m.offsetHdrN[1]) / m.offsetHdrD[1],
              offset_hdr[1], kEpsilon);
  EXPECT_NEAR(static_cast<double>(m.offsetHdrN[2]) / m.offsetHdrD[2],
              offset_hdr[2], kEpsilon);

  EXPECT_NEAR(static_cast<double>(m.hdrCapacityMinN) / m.hdrCapacityMinD,
              hdr_capacity_min, kEpsilon);
  EXPECT_NEAR(static_cast<double>(m.hdrCapacityMaxN) / m.hdrCapacityMaxD,
              hdr_capacity_max, kEpsilon);
  EXPECT_EQ(m.baseRenditionIsHDR, base_rendition_is_hdr);
}

TEST(JpegTest, ReadJpegWithGainMap) {
  for (const char* filename : {"paris_exif_xmp_gainmap_bigendian.jpg",
                               "paris_exif_xmp_gainmap_littleendian.jpg"}) {
    SCOPED_TRACE(filename);

    const testutil::AvifImagePtr image =
        testutil::ReadImage(data_path, filename, AVIF_PIXEL_FORMAT_YUV444, 8,
                            AVIF_CHROMA_DOWNSAMPLING_AUTOMATIC,
                            /*ignore_icc=*/false, /*ignore_exif=*/false,
                            /*ignore_xmp=*/true, /*allow_changing_cicp=*/true,
                            /*ignore_gain_map=*/false);
    ASSERT_NE(image, nullptr);
    ASSERT_NE(image->gainMap.image, nullptr);
    EXPECT_EQ(image->gainMap.image->width, 512u);
    EXPECT_EQ(image->gainMap.image->height, 384u);
    // Since ignore_xmp is true, there should be no XMP, even if it had to
    // be read to parse the gain map.
    EXPECT_EQ(image->xmp.size, 0u);

    CheckGainMapMetadata(image->gainMap.metadata,
                         /*gain_map_min=*/{1.0, 1.0, 1.0},
                         /*gain_map_max=*/{exp2(3.5), exp2(3.6), exp2(3.7)},
                         /*gain_map_gamma=*/{1.0, 1.0, 1.0},
                         /*offset_sdr=*/{0.0, 0.0, 0.0},
                         /*offset_hdr=*/{0.0, 0.0, 0.0},
                         /*hdr_capacity_min=*/1.0,
                         /*hdr_capacity_max=*/exp2(3.5),
                         /*base_rendition_is_hdr=*/false);
  }
}

TEST(JpegTest, IgnoreGainMap) {
  const testutil::AvifImagePtr image = testutil::ReadImage(
      data_path, "paris_exif_xmp_gainmap_littleendian.jpg",
      AVIF_PIXEL_FORMAT_YUV444, 8, AVIF_CHROMA_DOWNSAMPLING_AUTOMATIC,
      /*ignore_icc=*/false, /*ignore_exif=*/false,
      /*ignore_xmp=*/false, /*allow_changing_cicp=*/true,
      /*ignore_gain_map=*/true);
  ASSERT_NE(image, nullptr);
  EXPECT_EQ(image->gainMap.image, nullptr);
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
  avifGainMapMetadata metadata;
  EXPECT_TRUE(avifJPEGParseGainMapXMP((const uint8_t*)xmp.data(), xmp.size(),
                                      &metadata));

  CheckGainMapMetadata(
      metadata,
      /*gain_map_min=*/{exp2(0.025869), exp2(0.075191), exp2(0.142298)},
      /*gain_map_max=*/{exp2(3.527605), exp2(2.830234), exp2(1.537243)},
      /*gain_map_gamma=*/{1.0 / 0.506828, 1.0 / 0.590032, 1.0 / 1.517708},
      /*offset_sdr=*/{0.046983, 0.046983, 0.046983},
      /*offset_hdr=*/{0.046983, 0.046983, 0.046983},
      /*hdr_capacity_min=*/1.0,
      /*hdr_capacity_max=*/exp2(3.9),
      /*base_rendition_is_hdr=*/true);
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
  avifGainMapMetadata metadata;
  EXPECT_TRUE(avifJPEGParseGainMapXMP((const uint8_t*)xmp.data(), xmp.size(),
                                      &metadata));

  CheckGainMapMetadata(metadata,
                       /*gain_map_min=*/{1.0, 1.0, 1.0},
                       /*gain_map_max=*/{exp2(1.0), exp2(1.0), exp2(1.0)},
                       /*gain_map_gamma=*/{1.0, 1.0, 1.0},
                       /*offset_sdr=*/{1.0 / 64.0, 1.0 / 64.0, 1.0 / 64.0},
                       /*offset_hdr=*/{1.0 / 64.0, 1.0 / 64.0, 1.0 / 64.0},
                       /*hdr_capacity_min=*/1.0,
                       /*hdr_capacity_max=*/exp2(1.0),
                       /*base_rendition_is_hdr=*/false);
}

TEST(JpegTest, ExtendedXmp) {
  const std::string xmp = R"(
<?xpacket begin="" id="W5M0MpCehiHzreSzNTczkc9d"?>
<x:xmpmeta xmlns:x="adobe:ns:meta/">
  <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
    <rdf:Description rdf:about="stuff"
      xmlns:hdrgm="http://ns.adobe.com/hdr-gain-map/1.0/" hdrgm:Version="1.0"
      hdrgm:BaseRenditionIsHDR="True">
    </rdf:Description>
  </rdf:RDF>
</x:xmpmeta>
<?xpacket end="w"?>

<x:xmpmeta xmlns:x="adobe:ns:meta/">
  <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
    <!-- Imagine this is smoe exetended xmp that avifenc concatenated to
      the main XMP. As a result we have invalid XMP but should still be
      able to parse it. -->
    <stuff></stuff>
  </rdf:RDF>
</x:xmpmeta>
  )";
  avifGainMapMetadata metadata;
  EXPECT_TRUE(avifJPEGParseGainMapXMP((const uint8_t*)xmp.data(), xmp.size(),
                                      &metadata));

  // Note that this test passes because the gain map metadata is in the primary
  // XMP. If it was in the extended part, we wouldn't detect it (but probably
  // should).
  CheckGainMapMetadata(metadata,
                       /*gain_map_min=*/{1.0, 1.0, 1.0},
                       /*gain_map_max=*/{exp2(1.0), exp2(1.0), exp2(1.0)},
                       /*gain_map_gamma=*/{1.0, 1.0, 1.0},
                       /*offset_sdr=*/{1.0 / 64.0, 1.0 / 64.0, 1.0 / 64.0},
                       /*offset_hdr=*/{1.0 / 64.0, 1.0 / 64.0, 1.0 / 64.0},
                       /*hdr_capacity_min=*/1.0,
                       /*hdr_capacity_max=*/exp2(1.0),
                       /*base_rendition_is_hdr=*/true);
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
  avifGainMapMetadata metadata;
  EXPECT_FALSE(avifJPEGParseGainMapXMP((const uint8_t*)xmp.data(), xmp.size(),
                                       &metadata));
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
  avifGainMapMetadata metadata;
  EXPECT_FALSE(avifJPEGParseGainMapXMP((const uint8_t*)xmp.data(), xmp.size(),
                                       &metadata));
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
  avifGainMapMetadata metadata;
  EXPECT_FALSE(avifJPEGParseGainMapXMP((const uint8_t*)xmp.data(), xmp.size(),
                                       &metadata));
}

TEST(JpegTest, EmptyXMP) {
  const std::string xmp = "";
  avifGainMapMetadata metadata;
  EXPECT_FALSE(avifJPEGParseGainMapXMP((const uint8_t*)xmp.data(), xmp.size(),
                                       &metadata));
}

//------------------------------------------------------------------------------

}  // namespace
}  // namespace libavif

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  if (argc != 2) {
    std::cerr << "There must be exactly one argument containing the path to "
                 "the test data folder"
              << std::endl;
    return 1;
  }
  libavif::data_path = argv[1];
  return RUN_ALL_TESTS();
}
