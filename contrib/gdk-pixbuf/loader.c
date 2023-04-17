/* Copyright 2020 Emmanuel Gil Peyrot. All rights reserved.
   SPDX-License-Identifier: BSD-2-Clause
*/

#include <avif/avif.h>
#include <stdlib.h>

#define GDK_PIXBUF_ENABLE_BACKEND
#include <gdk-pixbuf/gdk-pixbuf-io.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

G_MODULE_EXPORT void fill_vtable (GdkPixbufModule * module);
G_MODULE_EXPORT void fill_info (GdkPixbufFormat * info);

G_BEGIN_DECLS

typedef struct {
    GdkPixbuf * pixbuf;
    uint64_t duration_ms;
} AvifAnimationFrame;

struct _AvifAnimation {
    GdkPixbufAnimation parent;
    GArray * frames;

    GdkPixbufModuleSizeFunc size_func;
    GdkPixbufModuleUpdatedFunc updated_func;
    GdkPixbufModulePreparedFunc prepared_func;
    gpointer user_data;

    avifDecoder * decoder;
    GByteArray * data;
    GBytes * bytes;
};

#define GDK_TYPE_AVIF_ANIMATION avif_animation_get_type()
G_DECLARE_FINAL_TYPE(AvifAnimation, avif_animation, GDK, AVIF_ANIMATION, GdkPixbufAnimation);

G_DEFINE_TYPE(AvifAnimation, avif_animation, GDK_TYPE_PIXBUF_ANIMATION);

struct _AvifAnimationIter {
    GdkPixbufAnimationIter parent_instance;
    AvifAnimation * animation;
    size_t current_frame;
    uint64_t time_offset;
};

#define GDK_TYPE_AVIF_ANIMATION_ITER avif_animation_iter_get_type()
G_DECLARE_FINAL_TYPE(AvifAnimationIter, avif_animation_iter, GDK, AVIF_ANIMATION_ITER, GdkPixbufAnimationIter);

G_DEFINE_TYPE(AvifAnimationIter, avif_animation_iter, GDK_TYPE_PIXBUF_ANIMATION_ITER);

/* Animation class functions */
static void avif_animation_finalize(GObject * obj)
{
    AvifAnimation * context = (AvifAnimation *)obj;
    if (!context)
        return;

    if (context->decoder) {
        avifDecoderDestroy(context->decoder);
        context->decoder = NULL;
    }

    if (context->data) {
        g_byte_array_unref(context->data);
        context->bytes = NULL;
    }

    if (context->bytes) {
        g_bytes_unref(context->bytes);
        context->bytes = NULL;
    }

    if (context->frames) {
        for(size_t i = 0; i < context->frames->len; i++){
            g_object_unref(g_array_index(context->frames, AvifAnimationFrame, i).pixbuf);
        }
        g_array_free(context->frames, TRUE);
    }
}

static gboolean avif_animation_is_static_image(GdkPixbufAnimation * animation)
{
    AvifAnimation * context = (AvifAnimation *)animation;
    return context->frames->len == 1;
}

static GdkPixbuf * avif_animation_get_static_image(GdkPixbufAnimation * animation)
{
    AvifAnimation * context = (AvifAnimation *)animation;
    return g_array_index(context->frames, AvifAnimationFrame, 0).pixbuf;
}

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
static GdkPixbufAnimationIter * avif_animation_get_iter(GdkPixbufAnimation * animation, const GTimeVal * start_time)
{
    AvifAnimationIter * iter = g_object_new(GDK_TYPE_AVIF_ANIMATION_ITER, NULL);

    iter->animation = (AvifAnimation *)animation;
    g_object_ref(iter->animation);

    iter->time_offset = start_time->tv_sec * 1000 + start_time->tv_usec / 1000;
    iter->current_frame = 0;

    return (GdkPixbufAnimationIter *)iter;
}
G_GNUC_END_IGNORE_DEPRECATIONS

static void avif_animation_get_size(GdkPixbufAnimation * animation, int * width, int * height)
{
    AvifAnimation * context = (AvifAnimation *)animation;
    if(width){
        *width = context->decoder->image->width;
    }
    if(height){
        *height = context->decoder->image->height;
    }
}

static void avif_animation_init(AvifAnimation * obj) {
    /* To ignore unused function and unused paremeter warnings/errors */
    (void)obj;
}
static void avif_animation_class_init(AvifAnimationClass * class) {
    class->parent_class.get_iter = avif_animation_get_iter;
    class->parent_class.get_size = avif_animation_get_size;
    class->parent_class.get_static_image = avif_animation_get_static_image;
    class->parent_class.is_static_image = avif_animation_is_static_image;
    G_OBJECT_CLASS(class)->finalize = avif_animation_finalize;
}

/* Iterator class functions */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
static gboolean avif_animation_iter_advance(GdkPixbufAnimationIter * iter, const GTimeVal * current_time)
{
    AvifAnimationIter * avif_iter = (AvifAnimationIter *)iter;
    AvifAnimation * context = (AvifAnimation *)avif_iter->animation;

    size_t prev_frame = avif_iter->current_frame;
    uint64_t elapsed_time = current_time->tv_sec * 1000 + current_time->tv_usec / 1000 - avif_iter->time_offset;

    /*
     * duration in seconds stored in a double which is cast to uint64_t
     * is the precision loss here significant?
     */
    uint64_t animation_time = (uint64_t)(context->decoder->duration * 1000);

    if (context->decoder->repetitionCount > 0 && elapsed_time > animation_time * context->decoder->repetitionCount) {
        avif_iter->current_frame = context->decoder->imageCount - 1;
    } else {
        elapsed_time = elapsed_time % animation_time;

        avif_iter->current_frame = 0;
        uint64_t frame_duration;

        while (1) {
            frame_duration = g_array_index(context->frames, AvifAnimationFrame, avif_iter->current_frame).duration_ms;

            if (elapsed_time <= frame_duration) {
                break;
            }
            elapsed_time -= frame_duration;
            avif_iter->current_frame++;
        }
    }

    return prev_frame != avif_iter->current_frame;
}
G_GNUC_END_IGNORE_DEPRECATIONS

static int avif_animation_iter_get_delay_time(GdkPixbufAnimationIter * iter)
{
    AvifAnimationIter * avif_iter = (AvifAnimationIter *)iter;
    return g_array_index(avif_iter->animation->frames, AvifAnimationFrame, avif_iter->current_frame).duration_ms;
}

static GdkPixbuf * avif_animation_iter_get_pixbuf(GdkPixbufAnimationIter * iter)
{
    AvifAnimationIter * avif_iter = (AvifAnimationIter *)iter;
    return g_array_index(avif_iter->animation->frames, AvifAnimationFrame, avif_iter->current_frame).pixbuf;
}

static gboolean avif_animation_iter_on_currently_loading_frame(GdkPixbufAnimationIter * iter)
{
    /* this function is effectively useless with how the rest of this module was written */
    (void)iter;
    return FALSE;
}

static void avif_animation_iter_finalize(GObject * obj)
{
    AvifAnimationIter * iter = (AvifAnimationIter *)obj;
    g_object_unref(iter->animation);
    g_object_unref(iter);
}


static void avif_animation_iter_init(AvifAnimationIter * obj) {
    /* To ignore unused function and unused paremeter warnings/errors */
    (void)obj;
}
static void avif_animation_iter_class_init(AvifAnimationIterClass * class) {
    class->parent_class.advance = avif_animation_iter_advance;
    class->parent_class.get_delay_time = avif_animation_iter_get_delay_time;
    class->parent_class.get_pixbuf = avif_animation_iter_get_pixbuf;
    class->parent_class.on_currently_loading_frame = avif_animation_iter_on_currently_loading_frame;
    G_OBJECT_CLASS(class)->finalize = avif_animation_iter_finalize;
}

G_END_DECLS

GdkPixbuf* set_pixbuf(AvifAnimation * context, GError ** error)
{
    avifResult ret;
    avifDecoder * decoder = context->decoder;
    GdkPixbuf * output;

    avifImage * image;
    avifRGBImage rgb;
    int width, height;
    image = decoder->image;
    width = image->width;
    height = image->height;

    avifRGBImageSetDefaults(&rgb, image);
    rgb.depth = 8;

    if (image->alphaPlane) {
        rgb.format = AVIF_RGB_FORMAT_RGBA;
        output = gdk_pixbuf_new(GDK_COLORSPACE_RGB,
                                TRUE, 8, width, height);
    } else {
        rgb.format = AVIF_RGB_FORMAT_RGB;
        output = gdk_pixbuf_new(GDK_COLORSPACE_RGB,
                                FALSE, 8, width, height);
    }

    if (output == NULL) {
        g_set_error_literal(error,
                            GDK_PIXBUF_ERROR,
                            GDK_PIXBUF_ERROR_INSUFFICIENT_MEMORY,
                            "Insufficient memory to open AVIF file");
        return NULL;
    }

    rgb.pixels = gdk_pixbuf_get_pixels(output);
    rgb.rowBytes = gdk_pixbuf_get_rowstride(output);

    ret = avifImageYUVToRGB(image, &rgb);
    if (ret != AVIF_RESULT_OK) {
        g_set_error(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_FAILED,
                    "Failed to convert YUV to RGB: %s", avifResultToString(ret));
        g_object_unref(output);
        return NULL;
    }

    /* transformations */
    if (image->transformFlags & AVIF_TRANSFORM_CLAP) {
        if ((image->clap.widthD > 0) && (image->clap.heightD > 0) &&
            (image->clap.horizOffD > 0) && (image->clap.vertOffD > 0)) {

            int new_width, new_height;

            new_width = (int)((double)(image->clap.widthN)  / (image->clap.widthD) + 0.5);
            if (new_width > width) {
                new_width = width;
            }

            new_height = (int)((double)(image->clap.heightN) / (image->clap.heightD) + 0.5);
            if (new_height > height) {
                new_height = height;
            }

            if (new_width > 0 && new_height > 0) {
                int offx, offy;
                GdkPixbuf *output_cropped;
                GdkPixbuf *cropped_copy;

                offx = ((double)((int32_t) image->clap.horizOffN)) / (image->clap.horizOffD) +
                       (width - new_width) / 2.0 + 0.5;
                if (offx < 0) {
                    offx = 0;
                } else if (offx > (width - new_width)) {
                    offx = width - new_width;
                }

                offy = ((double)((int32_t) image->clap.vertOffN)) / (image->clap.vertOffD) +
                       (height - new_height) / 2.0 + 0.5;
                if (offy < 0) {
                    offy = 0;
                } else if (offy > (height - new_height)) {
                    offy = height - new_height;
                }

                output_cropped = gdk_pixbuf_new_subpixbuf(output, offx, offy, new_width, new_height);
                cropped_copy = gdk_pixbuf_copy(output_cropped);
                g_clear_object(&output_cropped);

                if (cropped_copy) {
                    g_object_unref(output);
                    output = cropped_copy;
                }
            }
        } else {
            /* Zero values, we need to avoid 0 divide. */
            g_warning("Wrong values in avifCleanApertureBox\n");
        }
    }

    if (image->transformFlags & AVIF_TRANSFORM_IROT) {
        GdkPixbuf *output_rotated = NULL;

        switch (image->irot.angle) {
        case 1:
            output_rotated = gdk_pixbuf_rotate_simple(output, GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
            break;
        case 2:
            output_rotated = gdk_pixbuf_rotate_simple(output, GDK_PIXBUF_ROTATE_UPSIDEDOWN);
            break;
        case 3:
            output_rotated = gdk_pixbuf_rotate_simple(output, GDK_PIXBUF_ROTATE_CLOCKWISE);
            break;
        }

        if (output_rotated) {
          g_object_unref(output);
          output = output_rotated;
        }
    }

    if (image->transformFlags & AVIF_TRANSFORM_IMIR) {
        GdkPixbuf *output_mirrored = NULL;

        switch (image->imir.axis) {
        case 0:
            output_mirrored = gdk_pixbuf_flip(output, FALSE);
            break;
        case 1:
            output_mirrored = gdk_pixbuf_flip(output, TRUE);
            break;
        }

        if (output_mirrored) {
            g_object_unref(output);
            output = output_mirrored;
        }
    }

    /* width, height could be different after applied transformations */
    width = gdk_pixbuf_get_width(output);
    height = gdk_pixbuf_get_height(output);

    if (context->size_func) {
        (*context->size_func)(&width, &height, context->user_data);
    }

    if (width == 0 || height == 0) {
        g_set_error_literal(error,
                            GDK_PIXBUF_ERROR,
                            GDK_PIXBUF_ERROR_CORRUPT_IMAGE,
                            "Transformed AVIF has zero width or height");
        g_object_unref(output);
        return NULL;
    }

    if ( width < gdk_pixbuf_get_width(output) ||
         height < gdk_pixbuf_get_height(output)) {
        GdkPixbuf *output_scaled = NULL;

        output_scaled = gdk_pixbuf_scale_simple(output, width, height, GDK_INTERP_HYPER);
        if (output_scaled) {
            g_object_unref(output);
            output = output_scaled;
        }
    }

    if (image->icc.size != 0) {
        gchar *icc_base64 = g_base64_encode((const guchar *)image->icc.data, image->icc.size);
        gdk_pixbuf_set_option(output, "icc-profile", icc_base64);
        g_free(icc_base64);
    }

    return output;
}

static gboolean avif_context_try_load(AvifAnimation * context, GError ** error)
{

    context->frames = g_array_new(FALSE, TRUE, sizeof(AvifAnimationFrame));

    avifResult ret;
    avifDecoder * decoder = context->decoder;
    const uint8_t * data;
    size_t size;

    data = g_bytes_get_data(context->bytes, &size);

    ret = avifDecoderSetIOMemory(decoder, data, size);
    if (ret != AVIF_RESULT_OK) {
        g_set_error(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_CORRUPT_IMAGE,
                    "Couldn't decode image: %s", avifResultToString(ret));
        return FALSE;
    }

    ret = avifDecoderParse(decoder);
    if (ret != AVIF_RESULT_OK) {
        g_set_error(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_CORRUPT_IMAGE,
                    "Couldn't decode image: %s", avifResultToString(ret));
        return FALSE;
    }

    ret = avifDecoderNextImage(decoder);
    if (ret == AVIF_RESULT_NO_IMAGES_REMAINING) {
        /* No more images, bail out. Verify that you got the expected amount of images decoded. */
        return TRUE;
    } else if (ret != AVIF_RESULT_OK) {
        g_set_error(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_FAILED,
                    "Failed to decode all frames: %s", avifResultToString(ret));
        return FALSE;
    }

    AvifAnimationFrame frame;
    frame.pixbuf = set_pixbuf(context, error);
    frame.duration_ms = (uint64_t)(decoder->imageTiming.duration * 1000);

    if (frame.pixbuf == NULL) {
        return FALSE;
    }

    g_array_append_val(context->frames, frame);

    context->prepared_func(g_array_index(context->frames, AvifAnimationFrame, 0).pixbuf,
            decoder->imageCount > 1 ? (GdkPixbufAnimation *)context : NULL, context->user_data);

    while(decoder->imageIndex < decoder->imageCount - 1) {
        ret = avifDecoderNextImage(decoder);

        if (ret == AVIF_RESULT_NO_IMAGES_REMAINING) {
            return TRUE;
        } else if (ret != AVIF_RESULT_OK) {
            g_set_error(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_FAILED,
                        "Failed to decode all frames: %s", avifResultToString(ret));
            return FALSE;
        }

        frame.pixbuf = set_pixbuf(context, error);
        frame.duration_ms = (uint64_t)(decoder->imageTiming.duration * 1000);

        g_array_append_val(context->frames, frame);
    }
    return TRUE;
}

static gpointer begin_load(GdkPixbufModuleSizeFunc size_func,
                           GdkPixbufModulePreparedFunc prepared_func,
                           GdkPixbufModuleUpdatedFunc updated_func,
                           gpointer user_data, GError ** error)
{
    AvifAnimation * context;
    avifDecoder * decoder;

    g_assert(prepared_func != NULL);

    decoder = avifDecoderCreate();
    if (!decoder) {
        g_set_error_literal(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_INSUFFICIENT_MEMORY,
                            "Couldn't allocate memory for decoder");
        return NULL;
    }

    context = g_object_new(GDK_TYPE_AVIF_ANIMATION, NULL);
    if (!context)
        return NULL;

    context->size_func = size_func;
    context->updated_func = updated_func;
    context->prepared_func = prepared_func;
    context->user_data = user_data;

    context->decoder = decoder;
    context->data = g_byte_array_sized_new(40000);

    return context;
}

static gboolean stop_load(gpointer data, GError ** error)
{
    AvifAnimation * context = (AvifAnimation *) data;
    gboolean ret;

    context->bytes = g_byte_array_free_to_bytes(context->data);
    context->data = NULL;
    ret = avif_context_try_load(context, error);

    g_object_unref(context);

    return ret;
}

static gboolean load_increment(gpointer data, const guchar * buf, guint size, GError ** error)
{
    AvifAnimation * context = (AvifAnimation *) data;
    g_byte_array_append(context->data, buf, size);
    if (error)
        *error = NULL;
    return TRUE;
}

static gboolean avif_is_save_option_supported (const gchar *option_key)
{
    if (g_strcmp0(option_key, "quality") == 0) {
        return TRUE;
    }

    return FALSE;
}

static gboolean avif_image_saver(FILE          *f,
                                GdkPixbuf     *pixbuf,
                                gchar        **keys,
                                gchar        **values,
                                GError       **error)
{
    int width, height, min_quantizer, max_quantizer, alpha_quantizer;
    long quality = 52; /* default; must be between 0 and 100 */
    gboolean save_alpha;
    avifImage *avif;
    avifRGBImage rgb;
    avifResult res;
    avifRWData raw = AVIF_DATA_EMPTY;
    avifEncoder *encoder;
    guint maxThreads;

    if (f == NULL || pixbuf == NULL) {
        return FALSE;
    }

    if (keys && *keys) {
        gchar **kiter = keys;
        gchar **viter = values;

        while (*kiter) {
            if (strcmp(*kiter, "quality") == 0) {
                char *endptr = NULL;
                quality = strtol(*viter, &endptr, 10);

                if (endptr == *viter) {
                    g_set_error(error,
                                GDK_PIXBUF_ERROR,
                                GDK_PIXBUF_ERROR_BAD_OPTION,
                                "AVIF quality must be a value between 0 and 100; value \"%s\" could not be parsed.",
                                *viter);

                    return FALSE;
                }

                if (quality < 0 || quality > 100) {
                    g_set_error(error,
                                GDK_PIXBUF_ERROR,
                                GDK_PIXBUF_ERROR_BAD_OPTION,
                                "AVIF quality must be a value between 0 and 100; value \"%ld\" is not allowed.",
                                quality);

                    return FALSE;
                }
            } else {
                g_warning("Unrecognized parameter (%s) passed to AVIF saver.", *kiter);
            }

            ++kiter;
            ++viter;
        }
    }

    if (gdk_pixbuf_get_bits_per_sample(pixbuf) != 8) {
        g_set_error(error,
                    GDK_PIXBUF_ERROR,
                    GDK_PIXBUF_ERROR_UNKNOWN_TYPE,
                    "Sorry, only 8bit images are supported by this AVIF saver");
        return FALSE;
    }

    width = gdk_pixbuf_get_width(pixbuf);
    height = gdk_pixbuf_get_height(pixbuf);

    if ( width == 0 || height == 0) {
        g_set_error(error,
                    GDK_PIXBUF_ERROR,
                    GDK_PIXBUF_ERROR_CORRUPT_IMAGE,
                    "Empty image, nothing to save");
        return FALSE;
    }

    save_alpha = gdk_pixbuf_get_has_alpha(pixbuf);

    if (save_alpha) {
        if ( gdk_pixbuf_get_n_channels(pixbuf) != 4) {
            g_set_error(error,
                        GDK_PIXBUF_ERROR,
                        GDK_PIXBUF_ERROR_UNKNOWN_TYPE,
                        "Unsupported number of channels");
            return FALSE;
        }
    }
    else {
        if ( gdk_pixbuf_get_n_channels(pixbuf) != 3) {
            g_set_error(error,
                        GDK_PIXBUF_ERROR,
                        GDK_PIXBUF_ERROR_UNKNOWN_TYPE,
                        "Unsupported number of channels");
            return FALSE;
        }
    }

    max_quantizer = AVIF_QUANTIZER_WORST_QUALITY * (100 - (int)quality) / 100;
    min_quantizer = 0;
    alpha_quantizer = 0;

    if ( max_quantizer > 20 ) {
        min_quantizer = max_quantizer - 20;

        if (max_quantizer > 40) {
            alpha_quantizer = max_quantizer - 40;
        }
    }

    avif = avifImageCreate(width, height, 8, AVIF_PIXEL_FORMAT_YUV420);
    avif->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT601;
    avifRGBImageSetDefaults( &rgb, avif);

    rgb.depth = 8;
    rgb.pixels = (uint8_t*) gdk_pixbuf_read_pixels(pixbuf);
    rgb.rowBytes = gdk_pixbuf_get_rowstride(pixbuf);

    if (save_alpha) {
        rgb.format = AVIF_RGB_FORMAT_RGBA;
    } else {
        rgb.format = AVIF_RGB_FORMAT_RGB;
    }

    res = avifImageRGBToYUV(avif, &rgb);
    if ( res != AVIF_RESULT_OK ) {
        g_set_error(error,
                    GDK_PIXBUF_ERROR,
                    GDK_PIXBUF_ERROR_FAILED,
                    "Problem in RGB->YUV conversion: %s", avifResultToString(res));
        avifImageDestroy(avif);
        return FALSE;
    }

    maxThreads = g_get_num_processors();
    encoder = avifEncoderCreate();

    encoder->maxThreads = CLAMP(maxThreads, 1, 64);
    encoder->minQuantizer = min_quantizer;
    encoder->maxQuantizer = max_quantizer;
    encoder->minQuantizerAlpha = 0;
    encoder->maxQuantizerAlpha = alpha_quantizer;
    encoder->speed = 6;

    res = avifEncoderWrite(encoder, avif, &raw);
    avifEncoderDestroy(encoder);
    avifImageDestroy(avif);

    if ( res == AVIF_RESULT_OK ) {
        fwrite(raw.data, 1, raw.size, f);
        avifRWDataFree(&raw);
        return TRUE;
    }

    g_set_error(error,
                GDK_PIXBUF_ERROR,
                GDK_PIXBUF_ERROR_FAILED,
                "AVIF encoder problem: %s", avifResultToString(res));
    return FALSE;
}


G_MODULE_EXPORT void fill_vtable(GdkPixbufModule * module)
{
    module->begin_load = begin_load;
    module->stop_load = stop_load;
    module->load_increment = load_increment;
    module->is_save_option_supported = avif_is_save_option_supported;
    module->save = avif_image_saver;
}

G_MODULE_EXPORT void fill_info(GdkPixbufFormat * info)
{
    static GdkPixbufModulePattern signature[] = {
        { "    ftypavif", "zzz         ", 100 },    /* file begins with 'ftypavif' at offset 4 */
        { NULL, NULL, 0 }
    };
    static gchar * mime_types[] = {
        "image/avif",
        NULL
    };
    static gchar * extensions[] = {
        "avif",
        NULL
    };

    info->name = "avif";
    info->signature = (GdkPixbufModulePattern *)signature;
    info->description = "AV1 Image File Format";
    info->mime_types = (gchar **)mime_types;
    info->extensions = (gchar **)extensions;
    info->flags = GDK_PIXBUF_FORMAT_WRITABLE | GDK_PIXBUF_FORMAT_THREADSAFE;
    info->license = "BSD";
    info->disabled = FALSE;
}

