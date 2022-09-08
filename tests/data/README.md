# Test images

## Still images

### Files `kodim*`

License: released by the Eastman Kodak Company for unrestricted usage

Source: http://r0k.us/graphics/kodak/

### Files `cosmos*`

License: [Creative Commons Attribution license (reuse allowed)](https://creativecommons.org/licenses/by/3.0/legalcode)

Source: ["Cosmos Laundromat" movie](https://www.youtube.com/watch?v=Y-rmzh0PI3c) (frame at around 1 minute past the
beginning)

## Metadata

The structure can be displayed using `exiv2 -pS <file>`.

### File `paris_exif_xmp_icc.jpg`

License: [same as libavif](https://github.com/AOMediaCodec/libavif/blob/main/LICENSE)

Source: Personal photo edited with Gimp 2.10.

| address | marker      | length | data                                         |
|--------:|-------------|-------:|----------------------------------------------|
|       0 | 0xffd8 SOI  |        |                                              |
|       2 | 0xffe0 APP0 |     16 | `JFIF.....,.,.`                              |
|      20 | 0xffe1 APP1 |   1134 | `Exif..II*......................`            |
|    1156 | 0xffe1 APP1 |   3929 | `http://ns.adobe.com/xap/1.0/.<?x`           |
|    5087 | 0xffe2 APP2 |    612 | `ICC_PROFILE......T........mntrRG chunk 1/1` |
|         |             |        | ...                                          |

### File `paris_icc_exif_xmp.png`

License: [same as libavif](https://github.com/AOMediaCodec/libavif/blob/main/LICENSE)

Source: `paris_exif_xmp_icc.jpg` loaded with `avifReadImage()` then written with `avifPNGWrite()` (modified to output
metadata this way).

| address | marker | length | data                             |
|--------:|--------|-------:|----------------------------------|
|       8 | IHDR   |     13 |                                  |
|      33 | iCCP   |    386 | `libavif..(.u..+DQ..?3...E.....` |
|     431 | zTXt   |    631 | `Raw profile type exif..H...[..` |
|    1074 | zTXt   |   1076 | `Raw profile type xmp..X..YQ..8` |
|    2162 | IDAT   |        | ...                              |
|  160456 | IEND   |      0 |                                  |

### File `paris_icc_exif_xmp_at_end.png`

License: [same as libavif](https://github.com/AOMediaCodec/libavif/blob/main/LICENSE)

Source: `paris_exif_xmp_icc.jpg` loaded with `avifReadImage()` then written with `avifPNGWrite()` (modified to output
metadata this way).

| address | marker | length | data                             |
|--------:|--------|-------:|----------------------------------|
|       8 | IHDR   |     13 |                                  |
|      33 | iCCP   |    386 | `libavif..(.u..+DQ..?3...E.....` |
|    2162 | IDAT   |        | ...                              |
|  151474 | eXIf   |   1126 | `II*..........................`  |
|  152612 | tEXt   |   7832 | `Raw profile type xmp..XMP.0000` |
|  160456 | IEND   |      0 |                                  |

## Grid

### File `sofa_grid_1x5_420.avif`

License: [same as libavif](https://github.com/AOMediaCodec/libavif/blob/main/LICENSE)

Source: Personal photo converted with `avifenc --grid 1x5 --yuv 420` at
commit [632d131](https://github.com/AOMediaCodec/libavif/commit/632d13188f9b7faa40f20d870e792174b8b5b8e6).
