// Copyright 2020 Cloudinary. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include "svt-av1/EbSvtAv1.h"

#include "svt-av1/EbSvtAv1Enc.h"

#include <string.h>

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define SVT_FULL_VERSION STR(SVT_VERSION_MAJOR) "." STR(SVT_VERSION_MINOR) "." STR(SVT_VERSION_PATCHLEVEL)

typedef struct avifCodecInternal
{
    /* SVT-AV1 Encoder Handle */
    EbComponentType * svt_encoder;

    EbSvtAv1EncConfiguration * svt_config;
} avifCodecInternal;

static avifBool allocate_svt_buffers(EbBufferHeaderType ** input_buf);
static avifResult dequeue_frame(avifCodec * codec, avifCodecEncodeOutput * output, avifBool done_sending_pics);

static avifResult svtCodecEncodeImage(avifCodec * codec,
                                      avifEncoder * encoder,
                                      const avifImage * image,
                                      avifBool alpha,
                                      uint32_t addImageFlags,
                                      avifCodecEncodeOutput * output)
{
    avifResult result = AVIF_RESULT_UNKNOWN_ERROR;
    EbColorFormat color_format = EB_YUV420;

    if (!(addImageFlags & AVIF_ADD_IMAGE_FLAG_SINGLE)) {
        return AVIF_RESULT_INVALID_CODEC_SPECIFIC_OPTION;
    }

    int y_shift = 0;
    // EbColorRange svt_range;
    if (alpha) {
        // svt_range = (image->alphaRange == AVIF_RANGE_FULL) ? EB_CR_FULL_RANGE : EB_CR_STUDIO_RANGE;
        y_shift = 1;
    } else {
        // svt_range = (image->yuvRange == AVIF_RANGE_FULL) ? EB_CR_FULL_RANGE : EB_CR_STUDIO_RANGE;
        switch (image->yuvFormat) {
            case AVIF_PIXEL_FORMAT_YUV444:
                color_format = EB_YUV444;
                break;
            case AVIF_PIXEL_FORMAT_YUV422:
                color_format = EB_YUV422;
                break;
            case AVIF_PIXEL_FORMAT_YUV420:
                color_format = EB_YUV420;
                y_shift = 1;
                break;
            case AVIF_PIXEL_FORMAT_YUV400:
            case AVIF_PIXEL_FORMAT_NONE:
            default:
                return AVIF_RESULT_UNKNOWN_ERROR;
        }
    }

    EbSvtAv1EncConfiguration * svt_config = avifAlloc(sizeof(EbSvtAv1EncConfiguration));
    if (!svt_config)
        return AVIF_RESULT_UNKNOWN_ERROR;
    codec->internal->svt_config = svt_config;

    svt_av1_enc_init_handle(&codec->internal->svt_encoder, NULL, svt_config);
    svt_config->encoder_color_format = color_format;
    svt_config->encoder_bit_depth = (uint8_t)image->depth;
    svt_config->high_dynamic_range_input = image->depth > 8 ? AVIF_TRUE : AVIF_FALSE;

    svt_config->source_width = image->width;
    svt_config->source_height = image->height;
    svt_config->logical_processors = encoder->maxThreads;
    svt_config->enable_adaptive_quantization = AVIF_FALSE;
    // disable 2-pass
    svt_config->rc_firstpass_stats_out = AVIF_FALSE;
    svt_config->rc_twopass_stats_in = (SvtAv1FixedBuf) { NULL, 0 };

    if (alpha) {
        svt_config->min_qp_allowed = AVIF_CLAMP(encoder->minQuantizerAlpha, 0, 63);
        svt_config->max_qp_allowed = AVIF_CLAMP(encoder->maxQuantizerAlpha, 0, 63);
    } else {
        svt_config->min_qp_allowed = AVIF_CLAMP(encoder->minQuantizer, 0, 63);
        svt_config->qp = AVIF_CLAMP(encoder->maxQuantizer, 0, 63);
    }

    if (encoder->tileRowsLog2 != 0) {
        int tileRowsLog2 = AVIF_CLAMP(encoder->tileRowsLog2, 0, 6);
        svt_config->tile_rows = 1 << tileRowsLog2;
    }
    if (encoder->tileColsLog2 != 0) {
        int tileColsLog2 = AVIF_CLAMP(encoder->tileColsLog2, 0, 6);
        svt_config->tile_columns = 1 << tileColsLog2;
    }
    if (encoder->speed != AVIF_SPEED_DEFAULT) {
        int speed = AVIF_CLAMP(encoder->speed, 0, 8);
        svt_config->enc_mode = (int8_t)speed;
    }

    if (color_format == EB_YUV422 || image->depth > 10) {
        svt_config->profile = PROFESSIONAL_PROFILE;
    } else if (color_format == EB_YUV444) {
        svt_config->profile = HIGH_PROFILE;
    }

    EbErrorType res = svt_av1_enc_set_parameter(codec->internal->svt_encoder, svt_config);
    EbBufferHeaderType * input_buffer = NULL;
    if (res == EB_ErrorBadParameter) {
        goto cleanup;
    }

    res = svt_av1_enc_init(codec->internal->svt_encoder);
    if (res != EB_ErrorNone) {
        goto cleanup;
    }

    allocate_svt_buffers(&input_buffer);
    EbSvtIOFormat * input_picture_buffer = (EbSvtIOFormat *)input_buffer->p_buffer;

    int bytesPerPixel = image->depth > 8 ? 2 : 1;
    if (alpha) {
        input_picture_buffer->y_stride = image->alphaRowBytes / bytesPerPixel;
        input_picture_buffer->luma = image->alphaPlane;
        input_buffer->n_filled_len = image->alphaRowBytes * image->height;
    } else {
        input_picture_buffer->y_stride = image->yuvRowBytes[0] / bytesPerPixel;
        input_picture_buffer->luma = image->yuvPlanes[0];
        input_buffer->n_filled_len = image->yuvRowBytes[0] * image->height;
        uint32_t uvHeight = (image->height + y_shift) >> y_shift;
        input_picture_buffer->cb = image->yuvPlanes[1];
        input_buffer->n_filled_len += image->yuvRowBytes[1] * uvHeight;
        input_picture_buffer->cr = image->yuvPlanes[2];
        input_buffer->n_filled_len += image->yuvRowBytes[2] * uvHeight;
        input_picture_buffer->cb_stride = image->yuvRowBytes[1] / bytesPerPixel;
        input_picture_buffer->cr_stride = image->yuvRowBytes[2] / bytesPerPixel;
    }

    input_buffer->flags = 0;
    input_buffer->pts = 0;

    EbAv1PictureType frame_type = EB_AV1_INVALID_PICTURE;
    if (addImageFlags & AVIF_ADD_IMAGE_FLAG_FORCE_KEYFRAME) {
        frame_type = EB_AV1_KEY_PICTURE;
    }
    input_buffer->pic_type = frame_type;

    res = svt_av1_enc_send_picture(codec->internal->svt_encoder, input_buffer);
    if (res != EB_ErrorNone) {
        goto cleanup;
    }

    result = dequeue_frame(codec, output, AVIF_FALSE);
cleanup:
    if (input_buffer) {
        avifFree(input_buffer->p_buffer);
        input_buffer->p_buffer = NULL;
        avifFree(input_buffer);
        input_buffer = NULL;
    }
    return result;
}

static avifBool svtCodecEncodeFinish(avifCodec * codec, avifCodecEncodeOutput * output)
{
    EbErrorType ret = EB_ErrorNone;

    EbBufferHeaderType input_buffer;
    input_buffer.n_alloc_len = 0;
    input_buffer.n_filled_len = 0;
    input_buffer.n_tick_count = 0;
    input_buffer.p_app_private = NULL;
    input_buffer.flags = EB_BUFFERFLAG_EOS;
    input_buffer.p_buffer = NULL;

    // flush
    ret = svt_av1_enc_send_picture(codec->internal->svt_encoder, &input_buffer);

    if (ret != EB_ErrorNone)
        return AVIF_FALSE;

    return (dequeue_frame(codec, output, AVIF_TRUE) == AVIF_RESULT_OK);
}

const char * avifCodecVersionSvt(void)
{
    return SVT_FULL_VERSION;
}

static void svtCodecDestroyInternal(avifCodec * codec)
{
    if (codec->internal->svt_encoder) {
        svt_av1_enc_deinit(codec->internal->svt_encoder);
        svt_av1_enc_deinit_handle(codec->internal->svt_encoder);
        codec->internal->svt_encoder = NULL;
    }
    if (codec->internal->svt_config) {
        avifFree(codec->internal->svt_config);
        codec->internal->svt_config = NULL;
    }
    avifFree(codec->internal);
}

avifCodec * avifCodecCreateSvt(void)
{
    avifCodec * codec = (avifCodec *)avifAlloc(sizeof(avifCodec));
    memset(codec, 0, sizeof(struct avifCodec));
    codec->encodeImage = svtCodecEncodeImage;
    codec->encodeFinish = svtCodecEncodeFinish;
    codec->destroyInternal = svtCodecDestroyInternal;

    codec->internal = (struct avifCodecInternal *)avifAlloc(sizeof(avifCodecInternal));
    memset(codec->internal, 0, sizeof(struct avifCodecInternal));
    codec->internal->svt_encoder = (EbComponentType *)avifAlloc(sizeof(EbComponentType));
    return codec;
}

static avifBool allocate_svt_buffers(EbBufferHeaderType ** input_buf)
{
    *input_buf = avifAlloc(sizeof(EbBufferHeaderType));
    if (!(*input_buf)) {
        return AVIF_FALSE;
    }
    (*input_buf)->p_buffer = avifAlloc(sizeof(EbSvtIOFormat));
    if (!(*input_buf)->p_buffer) {
        return AVIF_FALSE;
    }
    memset((*input_buf)->p_buffer, 0, sizeof(EbSvtIOFormat));
    (*input_buf)->size = sizeof(EbBufferHeaderType);
    (*input_buf)->p_app_private = NULL;
    (*input_buf)->pic_type = EB_AV1_INVALID_PICTURE;

    return AVIF_TRUE;
}

static avifResult dequeue_frame(avifCodec * codec, avifCodecEncodeOutput * output, avifBool done_sending_pics)
{
    EbErrorType res;
    int encode_at_eos = 0;

    do {
        EbBufferHeaderType * output_buf = NULL;

        res = svt_av1_enc_get_packet(codec->internal->svt_encoder, &output_buf, (uint8_t)done_sending_pics);
        if (output_buf != NULL) {
            encode_at_eos = ((output_buf->flags & EB_BUFFERFLAG_EOS) == EB_BUFFERFLAG_EOS);
            if (output_buf->p_buffer && (output_buf->n_filled_len > 0)) {
                avifCodecEncodeOutputAddSample(output,
                                               output_buf->p_buffer,
                                               output_buf->n_filled_len,
                                               (output_buf->pic_type == EB_AV1_KEY_PICTURE));
            }
            svt_av1_enc_release_out_buffer(&output_buf);
        }
        output_buf = NULL;
    } while (res == EB_ErrorNone && !encode_at_eos);
    if (!done_sending_pics && ((res == EB_ErrorNone) || (res == EB_NoErrorEmptyQueue)))
        return AVIF_RESULT_OK;
    return (res == EB_ErrorNone ? AVIF_RESULT_OK : AVIF_RESULT_UNKNOWN_ERROR);
}
