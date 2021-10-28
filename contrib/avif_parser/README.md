# AVIF parser

The AVIF parser is a standalone library that can be used to extract the width,
height, bit depth and number of channels from an AVIF payload.

See `avif_parser.h` for details on the API and `avif_parser.c` for the
implementation. See `avif_parser_test.cc` for usage examples.

## How to use

```
struct AvifParserFeatures features;
if (AvifParserGetFeaturesWithSize(bytes, number_of_available_bytes,
                                  &features) == kAvifParserOk) {
  // Use 'features.width' etc.
}
```

## Build

`avif_parser.c` is written in C. To build from this directory:

```
mkdir build && \
cd build && \
cmake .. -DAVIF_BUILD_PARSER=ON && \
cmake --build . --config Release
```

## Test

Tests are automatically built with the above command if GoogleTest is installed.
`avif_parser_test.cc` is written in C++.

Run them with:

```
ctest .
```
