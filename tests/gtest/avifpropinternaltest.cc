// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <cmath>

#include "avif/internal.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

TEST(InternalPropertiesTest, KnownFound) {
  const uint8_t FTYP[4]{'f', 't', 'y', 'p'};
  ASSERT_EQ(avifIsKnownPropertyType(FTYP), AVIF_TRUE);
  const uint8_t MDAT[4]{'m', 'd', 'a', 't'};
  ASSERT_EQ(avifIsKnownPropertyType(MDAT), AVIF_TRUE);
  const uint8_t ISPE[4]{'i', 's', 'p', 'e'};
  ASSERT_EQ(avifIsKnownPropertyType(ISPE), AVIF_TRUE);
}

TEST(InternalPropertiesTest, UnknownNotFound) {
  const uint8_t SIEP[4]{'s', 'i', 'e', 'p'};
  ASSERT_EQ(avifIsKnownPropertyType(SIEP), AVIF_FALSE);
  const uint8_t MTXF[4]{'m', 't', 'x', 'f'};
  ASSERT_EQ(avifIsKnownPropertyType(MTXF), AVIF_FALSE);
}

TEST(InternalPropertiesTest, UuidValid) {
  const uint8_t uuid[16]{0x98, 0x10, 0xd7, 0xfc, 0xa5, 0xd2, 0x4c, 0x4b,
                         0x9a, 0x4f, 0x05, 0x99, 0x02, 0xf4, 0x9b, 0xfd};
  ASSERT_EQ(avifIsValidUUID(uuid), AVIF_TRUE);
}

TEST(InternalPropertiesTest, UuidInvalidISO) {
  const uint8_t uuid[16]{'m',  'd',  'a',  't',  0x00, 0x01, 0x00, 0x10,
                         0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};
  ASSERT_EQ(avifIsValidUUID(uuid), AVIF_FALSE);
}

TEST(InternalPropertiesTest, UuidInvalidVariant) {
  const uint8_t uuid[16]{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
                         0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  ASSERT_EQ(avifIsValidUUID(uuid), AVIF_FALSE);
}

TEST(InternalPropertiesTest, UuidInvalidVersion) {
  const uint8_t uuid[16]{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x00,
                         0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  ASSERT_EQ(avifIsValidUUID(uuid), AVIF_FALSE);
}

}  // namespace
}  // namespace avif
