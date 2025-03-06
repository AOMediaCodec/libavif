% AVIFDEC(1) | General Commands Manual
%
% 2022-04-30

<!--
This man page is written in pandoc's Markdown.
See: https://pandoc.org/MANUAL.html#pandocs-markdown
-->

# NAME

avifdec - decompress an AVIF file to an image file

# SYNOPSIS

**avifdec** [_options_] _input.avif_ _output._[_jpg_|_jpeg_|_png_|_y4m_]

**avifdec** **\--info** _input.avif_

# DESCRIPTION

**avifdec** decompresses an AVIF file to an image file.
Output format can be either JPEG, PNG or YUV4MPEG2 (Y4M).

# OPTIONS

**-h**, **\--help**
:   Show syntax help.

**-V**, **\--version**
:   Show the version number.

**-j**, **\--jobs** _J_
:   Number of jobs (worker threads), or 'all' to potentially use as many cores as possible. (Default: all).

**-c**, **\--codec** _C_
:   Codec to use.

    Possible values depend on the codecs enabled at build time (see **\--help**
    or **\--version** for the available codecs).
    Default is auto-selected from the available codecs.

    Possible values are:

    :   - **aom**
        - **dav1d**
        - **libgav1**

**-d**, **\--depth** _D_
:   Output depth, either 8 or 16. (PNG only; For y4m, depth is retained, and JPEG is always 8bpc).

**-q**, **\--quality** _Q_
:   Output quality in 0..100. (JPEG only, default: 90).

**\--png-compress** _L_
:   PNG compression level in 0..9 (PNG only; 0=none, 9=max). Defaults to libpng's builtin default.

**-u**, **\--upsampling** _U_
:   Chroma upsampling (for 420/422). One of 'automatic' (default), 'fastest', 'best', 'nearest', or 'bilinear'.

**-r**, **\--raw-color**
:   Output raw RGB values instead of multiplying by alpha when saving to opaque formats
    (JPEG only; not applicable to y4m).

**\--index** _I_
:   When decoding an image sequence or progressive image, specify which frame index to decode, where the first frame has index 0, or 'all' to decode all frames. (Default: 0)

**\--progressive**
:   Enable progressive AVIF processing. If a progressive image is encountered and \--progressive is passed,
    avifdec will use \--index to choose which layer to decode (in progressive order).

**\--no-strict**
:   Disable strict decoding, which disables strict validation checks and errors.

**-i**, **\--info**
:   Decode all frames and display all image information instead of saving to disk.

**\--icc** _FILENAME_
:   Provide an ICC profile payload (implies \--ignore-icc).

**\--ignore-icc**
:   If the input file contains an embedded ICC profile, ignore it (no-op if absent).

**\--size-limit** _C_
:   Maximum image size (in total pixels) that should be tolerated.
    (Default: 268435456).

**\--dimension-limit** _C_
:   Maximum image dimension (width or height) that should be tolerated.
    Set to 0 to ignore. (Default: 32768).

**\--**
:   Signal the end of options. Everything after this is interpreted as file names.

# EXAMPLES

Decompress an AVIF file to a PNG file:
:   $ **avifdec input.avif output.png**

# REPORTING BUGS

Bugs can be reported on GitHub at:
:   <https://github.com/AOMediaCodec/libavif/issues>

# SEE ALSO

**avifenc**(1)
