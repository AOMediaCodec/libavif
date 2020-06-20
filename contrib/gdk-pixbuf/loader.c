// Copyright 2020 Emmanuel Gil Peyrot. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include <avif/avif.h>

#define GDK_PIXBUF_ENABLE_BACKEND
#include <gdk-pixbuf/gdk-pixbuf-io.h>

G_MODULE_EXPORT void fill_vtable (GdkPixbufModule * module);
G_MODULE_EXPORT void fill_info (GdkPixbufFormat * info);

struct avif_context {
    GdkPixbuf * pixbuf;

    GdkPixbufModuleSizeFunc size_func;
    GdkPixbufModuleUpdatedFunc updated_func;
    GdkPixbufModulePreparedFunc prepared_func;
    gpointer user_data;

    avifDecoder * decoder;
    GByteArray * data;
    GBytes * bytes;
};

static void avif_context_free(struct avif_context * context)
{
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

    if (context->pixbuf) {
        g_object_unref(context->pixbuf);
        context->pixbuf = NULL;
    }

    g_free(context);
}

static gboolean avif_context_try_load(struct avif_context * context, GError ** error)
{
    avifResult ret;
    avifDecoder * decoder = context->decoder;
    avifImage * image;
    avifRGBImage rgb;
    avifROData raw;
    int width, height;

    raw.data = g_bytes_get_data(context->bytes, &raw.size);

    ret = avifDecoderParse(decoder, &raw);
    if (ret != AVIF_RESULT_OK) {
        g_set_error(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_CORRUPT_IMAGE,
                    "Couldn’t decode image: %s", avifResultToString(ret));
        return FALSE;
    }

    if (decoder->imageCount > 1) {
        g_set_error_literal(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_FAILED,
                            "Image sequences not yet implemented");
        return FALSE;
    }

    ret = avifDecoderNextImage(decoder);
    if (ret == AVIF_RESULT_NO_IMAGES_REMAINING) {
        // No more images, bail out. Verify that you got the expected amount of images decoded.
        return TRUE;
    } else if (ret != AVIF_RESULT_OK) {
        g_set_error(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_FAILED,
                    "Failed to decode all frames: %s", avifResultToString(ret));
        return FALSE;
    }

    image = decoder->image;
    width = image->width;
    height = image->height;

    (*context->size_func)(&width, &height, context->user_data);

    if (width == 0 || height == 0) {
        g_set_error_literal(error,
                            GDK_PIXBUF_ERROR,
                            GDK_PIXBUF_ERROR_CORRUPT_IMAGE,
                            "Transformed AVIF has zero width or height");
        return FALSE;
    }

    if (!context->pixbuf) {
        int bits_per_sample = 8;

        context->pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB,
                                         !!image->alphaPlane, bits_per_sample,
                                         width, height);
        if (context->pixbuf == NULL) {
            g_set_error_literal(error,
                                GDK_PIXBUF_ERROR,
                                GDK_PIXBUF_ERROR_INSUFFICIENT_MEMORY,
                                "Insufficient memory to open AVIF file");
            return FALSE;
        }
        context->prepared_func(context->pixbuf, NULL, context->user_data);
    }

    avifRGBImageSetDefaults(&rgb, image);
    rgb.depth = 8;
    rgb.format = image->alphaPlane ? AVIF_RGB_FORMAT_RGBA : AVIF_RGB_FORMAT_RGB;
    rgb.pixels = gdk_pixbuf_get_pixels(context->pixbuf);
    rgb.rowBytes = gdk_pixbuf_get_rowstride(context->pixbuf);

    ret = avifImageYUVToRGB(image, &rgb);
    if (ret != AVIF_RESULT_OK) {
        g_set_error(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_FAILED,
                    "Failed to convert YUV to RGB: %s", avifResultToString(ret));
        return FALSE;
    }

    return TRUE;
}

static gpointer begin_load(GdkPixbufModuleSizeFunc size_func,
                           GdkPixbufModulePreparedFunc prepared_func,
                           GdkPixbufModuleUpdatedFunc updated_func,
                           gpointer user_data, GError ** error)
{
    struct avif_context * context;
    avifDecoder * decoder;

    g_assert(size_func != NULL);
    g_assert(prepared_func != NULL);
    g_assert(updated_func != NULL);

    decoder = avifDecoderCreate();
    if (!decoder) {
        g_set_error_literal(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_INSUFFICIENT_MEMORY,
                            "Couldn’t allocate memory for decoder");
        return NULL;
    }

    context = g_new0(struct avif_context, 1);
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
    struct avif_context * context = (struct avif_context *) data;
    gboolean ret;

    context->bytes = g_byte_array_free_to_bytes(context->data);
    context->data = NULL;
    ret = avif_context_try_load(context, error);

    avif_context_free(context);

    return ret;
}

static gboolean load_increment(gpointer data, const guchar * buf, guint size, GError ** error)
{
    struct avif_context * context = (struct avif_context *) data;
    g_byte_array_append(context->data, buf, size);
    *error = NULL;
    return TRUE;
}

G_MODULE_EXPORT void fill_vtable(GdkPixbufModule * module)
{
    module->begin_load = begin_load;
    module->stop_load = stop_load;
    module->load_increment = load_increment;
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
    info->flags = GDK_PIXBUF_FORMAT_THREADSAFE;
    info->license = "BSD";
    info->disabled = FALSE;
}

