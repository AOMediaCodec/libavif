# libyuv
This folder contains source code imported from libyuv as of `c60323de1756f489bd061e66aea9c3c73a8ef72c`, with modifications intended to keep them relatively small and simple.

# Importing from upstream
When importing source code from upstream libyuv, the following changes must be done:
1. Source hierachy is to be kept the same as in libyuv.
2. The only APIs that are called by libavif for scaling are `ScalePlane` and `ScalePlane_12`. Anything else must be left out.
3. In function `ScalePlane` and `ScalePlane_16`, only the `ScalePlaneVertical`, `CopyPlane`, `ScalePlaneBox`, `ScalePlaneUp2_Linear`, `ScalePlaneUp2_Bilinear`, `ScalePlaneBilinearUp`, `ScalePlaneBilinearDown` and `ScalePlaneSimple` paths (and their `_16` equivalents) from libyuv must be kept; any other paths are to be stripped out.
4. `ScalePlane_12` simply calls `ScalePlane_16`, and is to have no special paths.
5. Anything unused and truly unneeeded by any of those functions must be thoroughly stripped out.
6. SIMD paths are to be stripped out completely.
7. The commit hash of libyuv from where the files got imported in this README.md file must be updated if possible.
8. `LIBYUV_API` must be removed from any and all imported functions as these files are always built static.