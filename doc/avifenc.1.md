% AVIFENC(1) | General Commands Manual
%
% 2025-04-11

<!--
This man page is written in pandoc's Markdown.
See: https://pandoc.org/MANUAL.html#pandocs-markdown
-->

# NAME

avifenc - compress an image file to an AVIF file

# SYNOPSIS

**avifenc** [_options_] _input._[_jpg_|_jpeg_|_png_|_y4m_] _output.avif_

# DESCRIPTION

**avifenc** compresses an image file to an AVIF file.
Input format can be either JPEG, PNG or YUV4MPEG2 (Y4M).

# OPTIONS

**-h**, **\--help**
:   Show syntax help.

**-V**, **\--version**
:   Show the version number.

## BASIC OPTIONS

**-q**, **\--qcolor** _Q_
:   Quality for color in 0..100 where 100 is lossless.

**\--qalpha** _Q_
:   Quality for alpha in 0..100 where 100 is lossless.

**-s**, **\--speed** _S_
:   Encoder speed in 0..10 where 0 is the slowest, 10 is the fastest. Or 'default' or 'd' for codec internal defaults. (Default: 6).

## ADVANCED OPTIONS

**-j**, **\--jobs** _J_
:   Number of jobs (worker threads), or 'all' to potentially use as many cores as possible. (Default: all).

**\--no-overwrite**
:   Never overwrite existing output file.

**-o**, **\--output** _FILENAME_
:   Instead of using the last filename given as output, use this filename.

**-l**, **\--lossless**
:   Set all defaults to encode losslessly, and emit warnings when settings/input don't allow for it.

**-d**, **\--depth** _D_
:   Output depth, one of 8, 10 or 12. (JPEG/PNG only; For y4m or stdin, depth is retained).

**-y**, **\--yuv** _FORMAT_
:   Output format, one of 'auto' (default), 444, 422, 420 or 400. Ignored for y4m or stdin (y4m format is retained).

    For JPEG, auto honors the JPEG's internal format, if possible. For grayscale PNG, auto defaults to 400.\
    For all other cases, auto defaults to 444.

**-p**, **\--premultiply**
:   Premultiply color by the alpha channel and signal this in the AVIF.

**\--sharpyuv**
:   Use sharp RGB to YUV420 conversion (if supported). Ignored for y4m or if output is not 420.

**\--stdin**
:   Read y4m frames from stdin instead of files; no input filenames allowed, must set before offering output filename.

**\--cicp**, **\--nclx** _P_/_T_/_M_
:   Set CICP values (nclx colr box) (3 raw numbers, use **-r** to set range flag).

    - _P_ = color primaries
    - _T_ = transfer characteristics
    - _M_ = matrix coefficients

    Use 2 for any you wish to leave unspecified.

**-r**, **\--range** _RANGE_
:   YUV range, one of 'limited' or 'l', 'full' or 'f'. (JPEG/PNG only, default: full; For y4m or stdin, range is retained).

**\--target-size** _S_
:   Set target file size in bytes (up to 7 times slower)

**\--progressive**
:   EXPERIMENTAL: Automatically set parameters to encode a simple layered image supporting progressive rendering from a single input frame.

**\--layered**
:   EXPERIMENTAL: Encode a layered AVIF. Each input is encoded as one layer and at most 4 layers can be encoded.

**-g**, **\--grid** _MxN_
:   Encode a single-image grid AVIF with M cols & N rows. Either supply MxN identical W/H/D images, or a single
    image that can be evenly split into the MxN grid and follow AVIF grid image restrictions. The grid will adopt
    the color profile of the first image supplied.

**-c**, **\--codec** _C_
:   Codec to use.

    Possible values depend on the codecs enabled at build time (see **\--help**
    or **\--version** for the available codecs).
    Default is auto-selected from the available codecs.

    Possible values are:

    :   - **aom**
        - **rav1e**
        - **svt**

**\--exif** _FILENAME_
:   Provide an Exif metadata payload to be associated with the primary item (implies \--ignore-exif).

**\--xmp** _FILENAME_
:   Provide an XMP metadata payload to be associated with the primary item (implies \--ignore-xmp).

**\--icc** _FILENAME_
:   Provide an ICC profile payload to be associated with the primary item (implies \--ignore-icc).

**\--timescale**, **\--fps** _V_
:   Timescale for image sequences. If all frames are 1 timescale in length, this is equivalent to frames per second. (Default: 30)
    If neither duration nor timescale are set, avifenc will attempt to use the framerate stored in a y4m header, if present.

**-k**, **\--keyframe** _INTERVAL_
:   Maximum keyframe interval for image sequences (any set of _INTERVAL_ consecutive frames will have at least one keyframe). Set to 0 to disable (default).

**\--ignore-exif**
:   If the input file contains embedded Exif metadata, ignore it (no-op if absent).

**\--ignore-xmp**
:   If the input file contains embedded XMP metadata, ignore it (no-op if absent).

**\--ignore-profile**, **\--ignore-icc**
:   If the input file contains an embedded color profile, ignore it (no-op if absent).

**\--ignore-gain-map**
:   If the input file contains an embedded gain map, ignore it (no-op if absent).

**\--qgain-map** _Q_
:   Quality for the gain map in 0..100 where 100 is lossless.

**\--pasp** _H_,_V_
:   Add pasp property (aspect ratio). H=horizontal spacing, V=vertical spacing.

**\--crop** _CROPX_,_CROPY_,_CROPW_,_CROPH_
:   Add clap property (clean aperture), but calculated from a crop rectangle.

**\--clap** _WN_,_WD_,_HN_,_HD_,_HON_,_HOD_,_VON_,_VOD_
:   Add clap property (clean aperture). Width, Height, HOffset, VOffset (in numerator/denominator pairs).

**\--irot** _ANGLE_
:   Add irot property (rotation) in 0..3. Makes (90 * ANGLE) degree rotation anti-clockwise.

**\--imir** _AXIS_
:   Add imir property (mirroring). 0=top-to-bottom, 1=left-to-right.

**\--clli** _MaxCLL_,_MaxPALL_
:   Add clli property (content light level information).

**\--repetition-count** _N_
:   Number of times an animated image sequence will be repeated, or 'infinite' for infinite repetitions. (Default: infinite).

**\--**
:   Signal the end of options. Everything after this is interpreted as file names.

## UPDATABLE OPTIONS

The following options can optionally have a **:u** (or **:update**) suffix like **-q:u _Q_**, to apply only to input files appearing after the option:

**-q**, **\--qcolor** _Q_
:   Quality for color in 0..100 where 100 is lossless.

**\--qalpha** _Q_
:   Quality for alpha in 0..100 where 100 is lossless.

**\--qgain-map** _Q_
:   Quality for the gain map in 0..100 where 100 is lossless.

**\--tilerowslog2** _R_
:   log2 of number of tile rows in 0..6. (Default: 0).
    If specified, switch to manual tiling.

**\--tilecolslog2** _C_
:   log2 of number of tile columns in 0..6. (Default: 0).
    If specified, switch to manual tiling.

**\--autotiling**
:   Set \--tilerowslog2 and \--tilecolslog2 automatically.
    If specified, switch to automatic tiling.
    avifenc starts in automatic tiling mode.

**\--scaling-mode** _N_[/_D_]
:   EXPERIMENTAL: Set frame (layer) scaling mode as given fraction. If omitted, the denominator defaults to 1. (Default: 1/1).

**\--duration** _D_
:   Frame durations (in timescales) (default: 1). This option always applies to following inputs with or without the `:u` suffix.

**-a**, **\--advanced** _KEY_[=_VALUE_]
:   Pass an advanced, codec-specific key/value string pair directly to the codec. avifenc will warn on any not used by the codec.

## AOM-SPECIFIC ADVANCED OPTIONS

1. **_\<key>_=_\<value>_** applies to both the color (YUV) planes and the alpha plane (if present).
2. **color:_\<key>_=_\<value>_** or **c:_\<key>_=_\<value>_** applies only to the color (YUV) planes.
3. **alpha:_\<key>_=_\<value>_** or **a:_\<key>_=_\<value>_** applies only to the alpha plane (if present).
    Since the alpha plane is encoded as a monochrome image, the options that refer to the chroma planes,
    such as enable-chroma-deltaq=B, should not be used with the alpha plane. In addition, the film grain
    options are unlikely to make sense for the alpha plane.

When used with libaom 3.0.0 or later, any key-value pairs supported by the aom_codec_set_option() function
can be used. When used with libaom 2.0.x or older, the following key-value pairs can be used:

**aq-mode=_M_**
:   Adaptive quantization mode. 0=off (default), 1=variance, 2=complexity, 3=cyclic refresh.

**cq-level=_Q_**
:   Constant/Constrained Quality level in 0..63, end-usage must be set to cq or q.

**enable-chroma-deltaq=_B_**
:   Enable delta quantization in chroma planes. 0=disable (default), 1=enable.

**end-usage=_MODE_**
:   Rate control mode, one of 'vbr', 'cbr', 'cq', or 'q'

**sharpness=_S_**
:   Bias towards block sharpness in rate-distortion optimization of transform coefficients in  0..7. (Default: 0).

**tune=_METRIC_**
:   Tune the encoder for distortion metric, one of 'psnr' or 'ssim'. (Default: psnr).

**film-grain-test=_TEST_**
:   Film grain test vectors in 0..16. 0=none (default), 1=test1, 2=test2, ... 16=test16.

**film-grain-table=_FILENAME_**
:   Path to file containing film grain parameters.

# EXAMPLES

Compress a PNG file to an AVIF file:
:   $ **avifenc input.png output.avif**

# REPORTING BUGS

Bugs can be reported on GitHub at:
:   <https://github.com/AOMediaCodec/libavif/issues>

# SEE ALSO

**avifdec**(1)
