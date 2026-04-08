// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <android/api-level.h>
#include <android/bitmap.h>
#include <android/hardware_buffer.h>
#include <android/hardware_buffer_jni.h>
#include <android/log.h>
#include <cpu-features.h>
#include <jni.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <new>

#include "avif/avif.h"

#define LOG_TAG "avif_jni"
#define LOGE(...) \
  ((void)__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__))

#define FUNC(RETURN_TYPE, NAME, ...)                                      \
  extern "C" {                                                            \
  JNIEXPORT RETURN_TYPE Java_org_aomedia_avif_android_AvifDecoder_##NAME( \
      JNIEnv* env, jobject thiz, ##__VA_ARGS__);                          \
  }                                                                       \
  JNIEXPORT RETURN_TYPE Java_org_aomedia_avif_android_AvifDecoder_##NAME( \
      JNIEnv* env, jobject thiz, ##__VA_ARGS__)

#define IGNORE_UNUSED_JNI_PARAMETERS \
  (void) env; \
  (void) thiz

namespace {

// RAII wrapper class that properly frees the decoder related objects on
// destruction.
struct AvifDecoderWrapper {
 public:
  AvifDecoderWrapper() = default;
  // Not copyable or movable.
  AvifDecoderWrapper(const AvifDecoderWrapper&) = delete;
  AvifDecoderWrapper& operator=(const AvifDecoderWrapper&) = delete;

  ~AvifDecoderWrapper() {
    if (decoder != nullptr) {
      avifDecoderDestroy(decoder);
    }
  }

  avifDecoder* decoder = nullptr;
  avifCropRect crop;
};

bool CreateDecoderAndParse(AvifDecoderWrapper* const decoder,
                           const uint8_t* const buffer, int length,
                           int threads) {
  decoder->decoder = avifDecoderCreate();
  if (decoder->decoder == nullptr) {
    LOGE("Failed to create AVIF Decoder.");
    return false;
  }
  decoder->decoder->maxThreads = threads;
  decoder->decoder->ignoreXMP = AVIF_TRUE;
  decoder->decoder->ignoreExif = AVIF_TRUE;

  // Turn off libavif's 'clap' (clean aperture) property validation. This allows
  // us to detect and ignore streams that have an invalid 'clap' property
  // instead failing.
  decoder->decoder->strictFlags &= ~AVIF_STRICT_CLAP_VALID;
  // Allow 'pixi' (pixel information) property to be missing. Older versions of
  // libheif did not add the 'pixi' item property to AV1 image items (See
  // crbug.com/1198455).
  decoder->decoder->strictFlags &= ~AVIF_STRICT_PIXI_REQUIRED;

  avifResult res = avifDecoderSetIOMemory(decoder->decoder, buffer, length);
  if (res != AVIF_RESULT_OK) {
    LOGE("Failed to set AVIF IO to a memory reader.");
    return false;
  }
  res = avifDecoderParse(decoder->decoder);
  if (res != AVIF_RESULT_OK) {
    LOGE("Failed to parse AVIF image: %s.", avifResultToString(res));
    return false;
  }

  avifDiagnostics diag;
  // If the image does not have a valid 'clap' property, then we simply display
  // the whole image.
  // TODO(vigneshv): Handle the case of avifCropRectRequiresUpsampling()
  //                 returning true.
  if (!(decoder->decoder->image->transformFlags & AVIF_TRANSFORM_CLAP) ||
      !avifCropRectFromCleanApertureBox(
          &decoder->crop, &decoder->decoder->image->clap,
          decoder->decoder->image->width, decoder->decoder->image->height,
          &diag) ||
      avifCropRectRequiresUpsampling(&decoder->crop,
                                     decoder->decoder->image->yuvFormat)) {
    decoder->crop.width = decoder->decoder->image->width;
    decoder->crop.height = decoder->decoder->image->height;
    decoder->crop.x = 0;
    decoder->crop.y = 0;
  }
  return true;
}

avifImage* ApplyCrop(
    AvifDecoderWrapper* const decoder,
    std::unique_ptr<avifImage, decltype(&avifImageDestroy)>& cropped_image) {
  if (decoder->decoder->image->width == decoder->crop.width &&
      decoder->decoder->image->height == decoder->crop.height &&
      decoder->crop.x == 0 && decoder->crop.y == 0) {
    return decoder->decoder->image;
  }
  cropped_image.reset(avifImageCreateEmpty());
  if (cropped_image == nullptr) {
    LOGE("Failed to allocate cropped image.");
    return nullptr;
  }
  avifResult res = avifImageSetViewRect(cropped_image.get(),
                                        decoder->decoder->image,
                                        &decoder->crop);
  if (res != AVIF_RESULT_OK) {
    LOGE("Failed to set crop rectangle. Status: %d", res);
    return nullptr;
  }
  return cropped_image.get();
}

avifResult AvifImageToBitmap(JNIEnv* const env,
                             AvifDecoderWrapper* const decoder,
                             jobject bitmap) {
  AndroidBitmapInfo bitmap_info;
  if (AndroidBitmap_getInfo(env, bitmap, &bitmap_info) < 0) {
    LOGE("AndroidBitmap_getInfo failed.");
    return AVIF_RESULT_UNKNOWN_ERROR;
  }
  // Ensure that the bitmap format is RGBA_8888, RGB_565 or RGBA_F16.
  if (bitmap_info.format != ANDROID_BITMAP_FORMAT_RGBA_8888 &&
      bitmap_info.format != ANDROID_BITMAP_FORMAT_RGB_565 &&
      bitmap_info.format != ANDROID_BITMAP_FORMAT_RGBA_F16) {
    LOGE("Bitmap format (%d) is not supported.", bitmap_info.format);
    return AVIF_RESULT_NOT_IMPLEMENTED;
  }
  void* bitmap_pixels = nullptr;
  if (AndroidBitmap_lockPixels(env, bitmap, &bitmap_pixels) !=
      ANDROID_BITMAP_RESULT_SUCCESS) {
    LOGE("Failed to lock Bitmap.");
    return AVIF_RESULT_UNKNOWN_ERROR;
  }
  avifImage* image;
  std::unique_ptr<avifImage, decltype(&avifImageDestroy)> cropped_image(
      nullptr, avifImageDestroy);
  avifResult res;
  if (decoder->decoder->image->width == decoder->crop.width &&
      decoder->decoder->image->height == decoder->crop.height &&
      decoder->crop.x == 0 && decoder->crop.y == 0) {
    image = decoder->decoder->image;
  } else {
    cropped_image.reset(avifImageCreateEmpty());
    if (cropped_image == nullptr) {
      LOGE("Failed to allocate cropped image.");
      return AVIF_RESULT_OUT_OF_MEMORY;
    }
    res = avifImageSetViewRect(cropped_image.get(), decoder->decoder->image,
                               &decoder->crop);
    if (res != AVIF_RESULT_OK) {
      LOGE("Failed to set crop rectangle. Status: %d", res);
      return res;
    }
    image = cropped_image.get();
  }
  std::unique_ptr<avifImage, decltype(&avifImageDestroy)> image_copy(
      nullptr, avifImageDestroy);
  if (image->width != bitmap_info.width ||
      image->height != bitmap_info.height) {
    // If the avifImage does not own the planes, then create a copy for safe
    // scaling.
    if (!image->imageOwnsYUVPlanes || !image->imageOwnsAlphaPlane) {
      image_copy.reset(avifImageCreateEmpty());
      if (image_copy == nullptr) {
        LOGE("Failed to allocate image for scaling.");
        return AVIF_RESULT_OUT_OF_MEMORY;
      }
      res = avifImageCopy(image_copy.get(), image, AVIF_PLANES_ALL);
      if (res != AVIF_RESULT_OK) {
        LOGE("Failed to make a copy of the image for scaling. Status: %d", res);
        return res;
      }
      image = image_copy.get();
    }
    avifDiagnostics diag;
    res = avifImageScale(image, bitmap_info.width, bitmap_info.height, &diag);
    if (res != AVIF_RESULT_OK) {
      LOGE("Failed to scale image. Status: %d", res);
      return res;
    }
  }

  avifRGBImage rgb_image;
  avifRGBImageSetDefaults(&rgb_image, image);
  if (bitmap_info.format == ANDROID_BITMAP_FORMAT_RGBA_F16) {
    rgb_image.depth = 16;
    rgb_image.isFloat = AVIF_TRUE;
  } else if (bitmap_info.format == ANDROID_BITMAP_FORMAT_RGB_565) {
    rgb_image.format = AVIF_RGB_FORMAT_RGB_565;
    rgb_image.depth = 8;
  } else {
    rgb_image.depth = 8;
  }
  rgb_image.pixels = static_cast<uint8_t*>(bitmap_pixels);
  rgb_image.rowBytes = bitmap_info.stride;
  // Android always sees the Bitmaps as premultiplied with alpha when it renders
  // them:
  // https://developer.android.com/reference/android/graphics/Bitmap#setPremultiplied(boolean)
  rgb_image.alphaPremultiplied = AVIF_TRUE;
  res = avifImageYUVToRGB(image, &rgb_image);
  AndroidBitmap_unlockPixels(env, bitmap);
  if (res != AVIF_RESULT_OK) {
    LOGE("Failed to convert YUV Pixels to RGB. Status: %d", res);
    return res;
  }
  return AVIF_RESULT_OK;
}

avifResult DecodeNextImage(JNIEnv* const env, AvifDecoderWrapper* const decoder,
                           jobject bitmap) {
  avifResult res = avifDecoderNextImage(decoder->decoder);
  if (res != AVIF_RESULT_OK) {
    LOGE("Failed to decode AVIF image. Status: %d", res);
    return res;
  }
  return AvifImageToBitmap(env, decoder, bitmap);
}

avifResult DecodeNthImage(JNIEnv* const env, AvifDecoderWrapper* const decoder,
                          uint32_t n, jobject bitmap) {
  avifResult res = avifDecoderNthImage(decoder->decoder, n);
  if (res != AVIF_RESULT_OK) {
    LOGE("Failed to decode AVIF image. Status: %d", res);
    return res;
  }
  return AvifImageToBitmap(env, decoder, bitmap);
}

int getThreadCount(int threads) {
  if (threads < 0) {
    return android_getCpuCount();
  }
  if (threads == 0) {
    // Empirically, on Android devices with more than 1 core, decoding with 2
    // threads is almost always better than using as many threads as CPU cores.
    return std::min(android_getCpuCount(), 2);
  }
  return threads;
}

// Checks if there is a pending JNI exception that will be thrown when the
// control returns to the java layer. If there is none, it will return false. If
// there is one, then it will clear the pending exception and return true.
// Whenever this function returns true, the caller should treat it as a fatal
// error and return with a failure status as early as possible.
bool JniExceptionCheck(JNIEnv* env) {
  if (!env->ExceptionCheck()) {
    return false;
  }
  env->ExceptionClear();
  return true;
}

AHardwareBuffer* TryAllocateHardwareBuffer(uint32_t width, uint32_t height,
                                           uint32_t format) {
  AHardwareBuffer_Desc desc = {};
  desc.width = width;
  desc.height = height;
  desc.layers = 1;
  desc.format = format;
  desc.usage = AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN |
               AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
  // On API 29+, check if the format is supported before allocating.
  if (android_get_device_api_level() >= 29) {
    if (!AHardwareBuffer_isSupported(&desc)) {
      return nullptr;
    }
  }
  AHardwareBuffer* buffer = nullptr;
  if (AHardwareBuffer_allocate(&desc, &buffer) != 0) {
    return nullptr;
  }
  return buffer;
}

AHardwareBuffer* TryDirectDecode(avifImage* image, uint32_t ahb_format,
                                 avifRGBFormat rgb_format, int rgb_depth,
                                 avifBool is_float, int bytes_per_pixel) {
  AHardwareBuffer* hwb =
      TryAllocateHardwareBuffer(image->width, image->height, ahb_format);
  if (hwb == nullptr) return nullptr;

  AHardwareBuffer_Desc desc;
  AHardwareBuffer_describe(hwb, &desc);

  void* pixels = nullptr;
  if (AHardwareBuffer_lock(hwb, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1,
                           nullptr, &pixels) != 0 ||
      pixels == nullptr) {
    AHardwareBuffer_release(hwb);
    return nullptr;
  }

  avifRGBImage rgb;
  avifRGBImageSetDefaults(&rgb, image);
  rgb.format = rgb_format;
  rgb.depth = rgb_depth;
  rgb.isFloat = is_float;
  rgb.pixels = static_cast<uint8_t*>(pixels);
  // AHardwareBuffer_Desc.stride is in pixels, not bytes.
  rgb.rowBytes = desc.stride * bytes_per_pixel;
  rgb.alphaPremultiplied = AVIF_TRUE;

  avifResult res = avifImageYUVToRGB(image, &rgb);
  AHardwareBuffer_unlock(hwb, nullptr);
  if (res != AVIF_RESULT_OK) {
    LOGE("avifImageYUVToRGB failed: %d", res);
    AHardwareBuffer_release(hwb);
    return nullptr;
  }
  return hwb;
}

AHardwareBuffer* AvifImageToHardwareBuffer(avifImage* image, bool allow_hdr,
                                           uint32_t* out_format) {
  if (allow_hdr && image->depth > 8) {
    AHardwareBuffer* hwb =
        TryDirectDecode(image, AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT,
                        AVIF_RGB_FORMAT_RGBA, 16, AVIF_TRUE, 8);
    if (hwb != nullptr) {
      *out_format = AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT;
      return hwb;
    }
  }
  AHardwareBuffer* hwb =
      TryDirectDecode(image, AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
                      AVIF_RGB_FORMAT_RGBA, 8, AVIF_FALSE, 4);
  if (hwb != nullptr) {
    *out_format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
  }
  return hwb;
}

// avifImageYUVToRGB preserves the source transfer function and does not
// tone-map, so PQ/HLG images must be tagged with the matching HDR color space.
jobject GetColorSpace(JNIEnv* env, const avifImage* image,
                      uint32_t ahb_format) {
  if (ahb_format != AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT) {
    return nullptr;
  }
  const int api = android_get_device_api_level();
  const bool is_bt2020 =
      (image->colorPrimaries == AVIF_COLOR_PRIMARIES_BT2020);
  // Look up ColorSpace.get(ColorSpace.Named.<name>).
  auto get_named_cs = [&](const char* name) -> jobject {
    jclass cs_named = env->FindClass("android/graphics/ColorSpace$Named");
    if (cs_named == nullptr) {
      if (env->ExceptionCheck()) env->ExceptionClear();
      return nullptr;
    }
    jfieldID fid = env->GetStaticFieldID(cs_named, name,
                                         "Landroid/graphics/ColorSpace$Named;");
    if (fid == nullptr) {
      if (env->ExceptionCheck()) env->ExceptionClear();
      return nullptr;
    }
    jobject named_val = env->GetStaticObjectField(cs_named, fid);
    if (named_val == nullptr) return nullptr;
    jclass cs = env->FindClass("android/graphics/ColorSpace");
    if (cs == nullptr) {
      if (env->ExceptionCheck()) env->ExceptionClear();
      return nullptr;
    }
    jmethodID get = env->GetStaticMethodID(
        cs, "get",
        "(Landroid/graphics/ColorSpace$Named;)Landroid/graphics/ColorSpace;");
    if (get == nullptr) {
      if (env->ExceptionCheck()) env->ExceptionClear();
      return nullptr;
    }
    jobject result = env->CallStaticObjectMethod(cs, get, named_val);
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
      return nullptr;
    }
    return result;
  };
  if (is_bt2020 &&
      image->transferCharacteristics == AVIF_TRANSFER_CHARACTERISTICS_PQ &&
      api >= 33) {
    jobject cs = get_named_cs("BT2020_PQ");
    if (cs != nullptr) return cs;
  }
  if (is_bt2020 &&
      image->transferCharacteristics == AVIF_TRANSFER_CHARACTERISTICS_HLG &&
      api >= 34) {
    jobject cs = get_named_cs("BT2020_HLG");
    if (cs != nullptr) return cs;
  }
  // FP16 with non-HDR transfer: gamma-encoded SDR content. Tag as SRGB, not
  // LINEAR_EXTENDED_SRGB.
  return get_named_cs("SRGB");
}

bool AvifImageToExistingHardwareBuffer(JNIEnv* env,
                                       AvifDecoderWrapper* decoder,
                                       jobject dest) {
  if (android_get_device_api_level() < 26) return false;

  // AHardwareBuffer_fromHardwareBuffer returns a borrowed pointer; the Java
  // HardwareBuffer retains ownership. Do not call AHardwareBuffer_release on
  // the returned pointer.
  AHardwareBuffer* ahb = AHardwareBuffer_fromHardwareBuffer(env, dest);
  if (ahb == nullptr) return false;

  std::unique_ptr<avifImage, decltype(&avifImageDestroy)> cropped_image(
      nullptr, avifImageDestroy);
  avifImage* image = ApplyCrop(decoder, cropped_image);
  if (image == nullptr) return false;

  AHardwareBuffer_Desc desc;
  AHardwareBuffer_describe(ahb, &desc);

  if (desc.width != image->width || desc.height != image->height) {
    LOGE("AvifImageToExistingHardwareBuffer: buffer %ux%u != image %ux%u",
         desc.width, desc.height, image->width, image->height);
    return false;
  }

  void* pixels = nullptr;
  if (AHardwareBuffer_lock(ahb, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1,
                           nullptr, &pixels) != 0 ||
      pixels == nullptr) {
    return false;
  }

  avifRGBImage rgb;
  avifRGBImageSetDefaults(&rgb, image);
  rgb.alphaPremultiplied = AVIF_TRUE;
  rgb.pixels = static_cast<uint8_t*>(pixels);

  bool ok = false;
  switch (desc.format) {
    case AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM:
      rgb.format = AVIF_RGB_FORMAT_RGBA;
      rgb.depth = 8;
      rgb.isFloat = AVIF_FALSE;
      rgb.rowBytes = desc.stride * 4;
      ok = avifImageYUVToRGB(image, &rgb) == AVIF_RESULT_OK;
      break;
    case AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT:
      rgb.format = AVIF_RGB_FORMAT_RGBA;
      rgb.depth = 16;
      rgb.isFloat = AVIF_TRUE;
      rgb.rowBytes = desc.stride * 8;
      ok = avifImageYUVToRGB(image, &rgb) == AVIF_RESULT_OK;
      break;
    default:
      LOGE("AvifImageToExistingHardwareBuffer: unsupported format 0x%x",
           desc.format);
      break;
  }

  AHardwareBuffer_unlock(ahb, nullptr);
  return ok;
}

jobject WrapHardwareBufferAsBitmap(JNIEnv* env, jobject java_hwb,
                                   jobject color_space) {
  jclass bitmap_class = env->FindClass("android/graphics/Bitmap");
  if (bitmap_class == nullptr) {
    if (env->ExceptionCheck()) env->ExceptionClear();
    return nullptr;
  }
  jmethodID wrap_method = env->GetStaticMethodID(
      bitmap_class, "wrapHardwareBuffer",
      "(Landroid/hardware/HardwareBuffer;Landroid/graphics/ColorSpace;)"
      "Landroid/graphics/Bitmap;");
  if (wrap_method == nullptr) {
    if (env->ExceptionCheck()) env->ExceptionClear();
    return nullptr;
  }
  jobject bitmap = env->CallStaticObjectMethod(bitmap_class, wrap_method,
                                               java_hwb, color_space);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return nullptr;
  }
  return bitmap;
}

// Decodes the current image into a hardware-backed Bitmap.
// If dest is non-null, decodes into that caller-provided HardwareBuffer and
// wraps it as a Bitmap (null color space — caller chose the format).
// If dest is null, allocates a new AHardwareBuffer, selects the color space
// from the image's CICP metadata, wraps as a Bitmap, and closes the
// intermediate Java HardwareBuffer (wrapHardwareBuffer holds its own ref).
jobject AvifImageToHardwareBitmap(JNIEnv* env, AvifDecoderWrapper* decoder,
                                  bool allow_hdr, jobject dest) {
  if (android_get_device_api_level() < 26) return nullptr;

  if (dest != nullptr) {
    if (!AvifImageToExistingHardwareBuffer(env, decoder, dest)) return nullptr;
    return WrapHardwareBufferAsBitmap(env, dest, /*color_space=*/nullptr);
  }

  std::unique_ptr<avifImage, decltype(&avifImageDestroy)> cropped_image(
      nullptr, avifImageDestroy);
  avifImage* image = ApplyCrop(decoder, cropped_image);
  if (image == nullptr) return nullptr;
  uint32_t ahb_format = 0;
  AHardwareBuffer* hwb =
      AvifImageToHardwareBuffer(image, allow_hdr, &ahb_format);
  if (hwb == nullptr) return nullptr;
  jobject java_hwb = AHardwareBuffer_toHardwareBuffer(env, hwb);
  // toHardwareBuffer increments the refcount; release the native reference now.
  AHardwareBuffer_release(hwb);
  if (java_hwb == nullptr) return nullptr;
  jobject color_space = GetColorSpace(env, image, ahb_format);
  jobject bitmap = WrapHardwareBufferAsBitmap(env, java_hwb, color_space);
  // Close the Java HardwareBuffer — wrapHardwareBuffer() holds its own ref.
  jclass hwb_class = env->FindClass("android/hardware/HardwareBuffer");
  if (hwb_class != nullptr) {
    jmethodID close = env->GetMethodID(hwb_class, "close", "()V");
    if (close != nullptr) env->CallVoidMethod(java_hwb, close);
    if (env->ExceptionCheck()) env->ExceptionClear();
  }
  return bitmap;
}

jobject CreateHardwareBufferForImage(JNIEnv* env, int width, int height,
                                     int depth, bool allow_hdr) {
  if (android_get_device_api_level() < 26) return nullptr;

  const uint64_t usage = AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN |
                         AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;

  auto try_alloc = [&](uint32_t format) -> AHardwareBuffer* {
    AHardwareBuffer_Desc desc = {};
    desc.width = static_cast<uint32_t>(width);
    desc.height = static_cast<uint32_t>(height);
    desc.layers = 1;
    desc.format = format;
    desc.usage = usage;
    if (android_get_device_api_level() >= 29 &&
        !AHardwareBuffer_isSupported(&desc)) {
      return nullptr;
    }
    AHardwareBuffer* hwb = nullptr;
    return (AHardwareBuffer_allocate(&desc, &hwb) == 0) ? hwb : nullptr;
  };

  AHardwareBuffer* hwb = nullptr;
  if (allow_hdr && depth > 8) {
    hwb = try_alloc(AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT);
  }
  if (hwb == nullptr) {
    hwb = try_alloc(AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM);
  }
  if (hwb == nullptr) return nullptr;

  jobject java_hwb = AHardwareBuffer_toHardwareBuffer(env, hwb);
  AHardwareBuffer_release(hwb);
  return java_hwb;
}

}  // namespace

jint JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
  JNIEnv* env;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    return -1;
  }
  return JNI_VERSION_1_6;
}

FUNC(jboolean, isAvifImage, jobject encoded, int length) {
  IGNORE_UNUSED_JNI_PARAMETERS;
  const uint8_t* const buffer =
      static_cast<const uint8_t*>(env->GetDirectBufferAddress(encoded));
  const avifROData avif = {buffer, static_cast<size_t>(length)};
  return avifPeekCompatibleFileType(&avif);
}

#define CHECK_EXCEPTION(ret)                \
  do {                                      \
    if (JniExceptionCheck(env)) return ret; \
  } while (false)

#define FIND_CLASS(var, class_name, ret)         \
  const jclass var = env->FindClass(class_name); \
  CHECK_EXCEPTION(ret);                          \
  if (var == nullptr) return ret

#define GET_FIELD_ID(var, class_name, field_name, signature, ret)          \
  const jfieldID var = env->GetFieldID(class_name, field_name, signature); \
  CHECK_EXCEPTION(ret);                                                    \
  if (var == nullptr) return ret

FUNC(jboolean, getInfo, jobject encoded, int length, jobject info) {
  IGNORE_UNUSED_JNI_PARAMETERS;
  const uint8_t* const buffer =
      static_cast<const uint8_t*>(env->GetDirectBufferAddress(encoded));
  AvifDecoderWrapper decoder;
  if (!CreateDecoderAndParse(&decoder, buffer, length, /*threads=*/1)) {
    return false;
  }
  FIND_CLASS(info_class, "org/aomedia/avif/android/AvifDecoder$Info", false);
  GET_FIELD_ID(width, info_class, "width", "I", false);
  GET_FIELD_ID(height, info_class, "height", "I", false);
  GET_FIELD_ID(depth, info_class, "depth", "I", false);
  GET_FIELD_ID(alpha_present, info_class, "alphaPresent", "Z", false);
  env->SetIntField(info, width, decoder.crop.width);
  CHECK_EXCEPTION(false);
  env->SetIntField(info, height, decoder.crop.height);
  CHECK_EXCEPTION(false);
  env->SetIntField(info, depth, decoder.decoder->image->depth);
  CHECK_EXCEPTION(false);
  env->SetBooleanField(info, alpha_present, decoder.decoder->alphaPresent);
  CHECK_EXCEPTION(false);
  return true;
}

FUNC(jboolean, decode, jobject encoded, int length, jobject bitmap,
     jint threads) {
  IGNORE_UNUSED_JNI_PARAMETERS;
  const uint8_t* const buffer =
      static_cast<const uint8_t*>(env->GetDirectBufferAddress(encoded));
  AvifDecoderWrapper decoder;
  if (!CreateDecoderAndParse(&decoder, buffer, length,
                             getThreadCount(threads))) {
    return false;
  }
  return DecodeNextImage(env, &decoder, bitmap) == AVIF_RESULT_OK;
}

FUNC(jlong, createDecoder, jobject encoded, jint length, jint threads) {
  const uint8_t* const buffer =
      static_cast<const uint8_t*>(env->GetDirectBufferAddress(encoded));
  std::unique_ptr<AvifDecoderWrapper> decoder(new (std::nothrow)
                                                  AvifDecoderWrapper());
  if (decoder == nullptr) {
    return 0;
  }
  if (!CreateDecoderAndParse(decoder.get(), buffer, length,
                             getThreadCount(threads))) {
    return 0;
  }
  FIND_CLASS(avif_decoder_class, "org/aomedia/avif/android/AvifDecoder", 0);
  GET_FIELD_ID(width_id, avif_decoder_class, "width", "I", 0);
  GET_FIELD_ID(height_id, avif_decoder_class, "height", "I", 0);
  GET_FIELD_ID(depth_id, avif_decoder_class, "depth", "I", 0);
  GET_FIELD_ID(alpha_present_id, avif_decoder_class, "alphaPresent", "Z", 0);
  GET_FIELD_ID(frame_count_id, avif_decoder_class, "frameCount", "I", 0);
  GET_FIELD_ID(repetition_count_id, avif_decoder_class, "repetitionCount", "I",
               0);
  GET_FIELD_ID(frame_durations_id, avif_decoder_class, "frameDurations", "[D",
               0);
  env->SetIntField(thiz, width_id, decoder->crop.width);
  CHECK_EXCEPTION(0);
  env->SetIntField(thiz, height_id, decoder->crop.height);
  CHECK_EXCEPTION(0);
  env->SetIntField(thiz, depth_id, decoder->decoder->image->depth);
  CHECK_EXCEPTION(0);
  env->SetBooleanField(thiz, alpha_present_id, decoder->decoder->alphaPresent);
  CHECK_EXCEPTION(0);
  env->SetIntField(thiz, repetition_count_id,
                   decoder->decoder->repetitionCount);
  CHECK_EXCEPTION(0);
  const int frameCount = decoder->decoder->imageCount;
  env->SetIntField(thiz, frame_count_id, frameCount);
  CHECK_EXCEPTION(0);
  // This native array is needed because setting one element at a time to a Java
  // array from the JNI layer is inefficient.
  std::unique_ptr<double[]> native_durations(
      new (std::nothrow) double[frameCount]);
  if (native_durations == nullptr) {
    return 0;
  }
  for (int i = 0; i < frameCount; ++i) {
    avifImageTiming timing;
    if (avifDecoderNthImageTiming(decoder->decoder, i, &timing) !=
        AVIF_RESULT_OK) {
      return 0;
    }
    native_durations[i] = timing.duration;
  }
  jdoubleArray durations = env->NewDoubleArray(frameCount);
  if (durations == nullptr) {
    return 0;
  }
  env->SetDoubleArrayRegion(durations, /*start=*/0, frameCount,
                            native_durations.get());
  CHECK_EXCEPTION(0);
  env->SetObjectField(thiz, frame_durations_id, durations);
  CHECK_EXCEPTION(0);
  return reinterpret_cast<jlong>(decoder.release());
}

#undef GET_FIELD_ID
#undef FIND_CLASS
#undef CHECK_EXCEPTION

FUNC(jint, nextFrame, jlong jdecoder, jobject bitmap) {
  IGNORE_UNUSED_JNI_PARAMETERS;
  AvifDecoderWrapper* const decoder =
      reinterpret_cast<AvifDecoderWrapper*>(jdecoder);
  return DecodeNextImage(env, decoder, bitmap);
}

FUNC(jint, nextFrameIndex, jlong jdecoder) {
  IGNORE_UNUSED_JNI_PARAMETERS;
  AvifDecoderWrapper* const decoder =
      reinterpret_cast<AvifDecoderWrapper*>(jdecoder);
  return decoder->decoder->imageIndex + 1;
}

FUNC(jint, nthFrame, jlong jdecoder, jint n, jobject bitmap) {
  IGNORE_UNUSED_JNI_PARAMETERS;
  AvifDecoderWrapper* const decoder =
      reinterpret_cast<AvifDecoderWrapper*>(jdecoder);
  return DecodeNthImage(env, decoder, n, bitmap);
}

FUNC(jstring, resultToString, jint result) {
  IGNORE_UNUSED_JNI_PARAMETERS;
  return env->NewStringUTF(avifResultToString(static_cast<avifResult>(result)));
}

FUNC(jstring, versionString) {
  IGNORE_UNUSED_JNI_PARAMETERS;
  char codec_versions[256];
  avifCodecVersions(codec_versions);
  char libyuv_version[64];
  if (avifLibYUVVersion() > 0) {
    snprintf(libyuv_version, sizeof(libyuv_version), " libyuv: %u.",
             avifLibYUVVersion());
  } else {
    libyuv_version[0] = '\0';
  }
  char version_string[512];
  snprintf(version_string, sizeof(version_string), "libavif: %s. Codecs: %s.%s",
           avifVersion(), codec_versions, libyuv_version);
  return env->NewStringUTF(version_string);
}

FUNC(void, destroyDecoder, jlong jdecoder) {
  IGNORE_UNUSED_JNI_PARAMETERS;
  AvifDecoderWrapper* const decoder =
      reinterpret_cast<AvifDecoderWrapper*>(jdecoder);
  delete decoder;
}

FUNC(jobject, nativeDecodeHardwareBitmap, jobject encoded, jint length,
     jint threads, jboolean allow_hdr) {
  IGNORE_UNUSED_JNI_PARAMETERS;
  const uint8_t* const buffer =
      static_cast<const uint8_t*>(env->GetDirectBufferAddress(encoded));
  AvifDecoderWrapper decoder;
  if (!CreateDecoderAndParse(&decoder, buffer, length,
                             getThreadCount(threads))) {
    return nullptr;
  }
  avifResult res = avifDecoderNextImage(decoder.decoder);
  if (res != AVIF_RESULT_OK) {
    LOGE("Failed to decode AVIF image. Status: %d", res);
    return nullptr;
  }
  return AvifImageToHardwareBitmap(env, &decoder, allow_hdr, /*dest=*/nullptr);
}

FUNC(jobject, nativeNextFrameHardwareBitmap, jlong jdecoder, jboolean allow_hdr,
     jobject dest) {
  IGNORE_UNUSED_JNI_PARAMETERS;
  AvifDecoderWrapper* const decoder =
      reinterpret_cast<AvifDecoderWrapper*>(jdecoder);
  avifResult res = avifDecoderNextImage(decoder->decoder);
  if (res != AVIF_RESULT_OK) {
    LOGE("Failed to decode AVIF image. Status: %d", res);
    return nullptr;
  }
  return AvifImageToHardwareBitmap(env, decoder, allow_hdr, dest);
}

FUNC(jobject, nativeNthFrameHardwareBitmap, jlong jdecoder, jint n,
     jboolean allow_hdr, jobject dest) {
  IGNORE_UNUSED_JNI_PARAMETERS;
  AvifDecoderWrapper* const decoder =
      reinterpret_cast<AvifDecoderWrapper*>(jdecoder);
  avifResult res = avifDecoderNthImage(decoder->decoder, n);
  if (res != AVIF_RESULT_OK) {
    LOGE("Failed to decode AVIF image. Status: %d", res);
    return nullptr;
  }
  return AvifImageToHardwareBitmap(env, decoder, allow_hdr, dest);
}

FUNC(jobject, nativeCreateHardwareBuffer, jint width, jint height, jint depth,
     jboolean allow_hdr) {
  IGNORE_UNUSED_JNI_PARAMETERS;
  return CreateHardwareBufferForImage(env, width, height, depth, allow_hdr);
}


