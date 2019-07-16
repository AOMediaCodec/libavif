#!/bin/bash

command -v colorist >/dev/null 2>&1 || { echo >&2 "Please install colorist."; exit 1; }

colorist generate "#ff0000" inputs/opaque_icc.avif -g 2.5
colorist generate "#ff0000" inputs/opaque_hdr10.avif -g pq -p bt2020 -l 10000

colorist generate "32x32,#ff0000" --yuv 420 inputs/opaque420.avif
colorist generate "32x32,#ff0000" --yuv 422 inputs/opaque422.avif
colorist generate "32x32,#ff0000" --yuv 444 inputs/opaque444.avif
colorist generate "32x32,#ff0000" inputs/opaque_lossless.avif -q 100

colorist generate "32x32,#ff000080" --yuv 420 inputs/alpha420.avif
colorist generate "32x32,#ff000080" --yuv 422 inputs/alpha422.avif
colorist generate "32x32,#ff000080" --yuv 444 inputs/alpha444.avif
colorist generate "32x32,#ff000080" inputs/alpha_lossless.avif -q 100
