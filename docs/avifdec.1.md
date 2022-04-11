% AVIFDEC(1) | General Commands Manual
%
% 2022-04-11

<!--
This man page is written in Pandoc's Markdown.
See: https://pandoc.org/MANUAL.html#pandocs-markdown
-->

# NAME

avifdec - decompress an AVIF file to an image file

# SYNOPSIS

**avifdec** [_options_] _input.avif_ _output._[_jpg_|_jpeg_|_png_|_y4m_]

**avifdec** **\--info** _input.avif_

# DESCRIPTION

**avifdec** decompress an AVIF file to an image file.
Output format can be either JPEG, PNG or YUV4MPEG2 (Y4M).

# OPTIONS

**-h**, **\--help**
:   Show syntax help.

**-V**, **\--version**
:   Show the version number.

**-j**, **\--jobs** _J_
:   Number of jobs (worker threads).
    Default is 1, and less than 1 is the same as 1.
    Use **all** to use all available cores.

**-c**, **\--codec** _C_
:   AV1 codec to use.
    Possible values depend on the codecs enabled at build time (see **\--help**
    or **\--version** for the available codecs).
    Default is auto-selected from the available codecs.

    Possible values:

    :   - **aom**
        - **dav1d**
        - **libgav1**

**-d**, **\--depth** _D_
:   Output depth.
    This is available if the output format is PNG, and for y4m, depth is
    retained, and JPEG is always 8bpc.

    Possible values:

    :   - **8**
        - **16**

**-q**, **\--quality** _Q_
:   Output quality.
    Possible values are **0**-**100**, and default is 90.
    This is available if the output format is JPEG.

**\--png-compress** _L_
:   Set PNG compression level.
    Possible values are **0**-**9** (none-max).
    Defaults to libpng's builtin default.
    This is available if the output format is PNG.

**-u**, **\--upsampling** _U_
:   Chroma upsampling (for 420/422).

    Possible values:

    :   - **automatic** (default)
        - **fastest**
        - **best**
        - **nearest**
        - **bilinear**

**-r**, **\--raw-color**
:   Output raw RGB values instead of multiplying by alpha when saving to opaque
    formats.
    This is available if the output format is JPEG, and not applicable to y4m.

**\--index** _I_
:   When decoding an image sequence or progressive image, specify which frame
    index to decode.
    Default is 0.

**\--progressive**
:   Enable progressive AVIF processing.
    If a progressive image is encountered and **\--progressive** is passed,
    **avifdec** will use **\--index** to choose which layer to decode (in
    progressive order).

**\--no-strict**
:   Disable strict decoding, which disables strict validation checks and errors.

**-i**, **\--info**
:   Decode all frames and display all image information instead of saving to
    disk.

**\--ignore-icc**
:   If the input file contains an embedded ICC profile, ignore it (no-op if
    absent).

**\--size-limit** _C_
:   Specifies the image size limit (in total pixels) that should be tolerated.
    Default is 268435456, and set to a smaller value to further restrict.

# EXAMPLES

Decompress an AVIF file to a PNG file:
:   $ **avifdec input.avif output.png**

# REPORTING BUGS

Bugs can be reported on GitHub at:
:   <https://github.com/AOMediaCodec/libavif/issues>

# SEE ALSO

**avifenc**(1)
