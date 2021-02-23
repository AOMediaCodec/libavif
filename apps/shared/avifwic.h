//
// Created by TYTY on 2021-02-22 022.
//

#ifndef LIBAVIF_APPS_SHARED_AVIFWIC_H
#define LIBAVIF_APPS_SHARED_AVIFWIC_H

#include "avif/avif.h"

avifBool avifWICRead(const char * inputFilename, avifImage * avif, avifPixelFormat requestedFormat, uint32_t requestedDepth, uint32_t * outDepth);

#endif //LIBAVIF_APPS_SHARED_AVIFWIC_H
