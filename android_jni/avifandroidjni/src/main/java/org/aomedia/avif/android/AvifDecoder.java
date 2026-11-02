// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

package org.aomedia.avif.android;

import android.graphics.Bitmap;
import android.hardware.HardwareBuffer;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.view.Display;
import androidx.annotation.Nullable;
import androidx.annotation.RequiresApi;
import java.nio.ByteBuffer;

/**
 * An AVIF Decoder. AVIF Specification: https://aomediacodec.github.io/av1-avif/.
 *
 * <p>There are two ways to use this class.
 *
 * <p>1) As a static utility class.
 *
 * <p>This class can be accessed statically without instantiating an object. This is useful to
 * simply sniff and decode still AVIF images without having to maintain any decoder state. The
 * following are the methods that can be accessed this way: {@link isAvifImage}, {@link getInfo} and
 * {@link decode}. The {@link Info} inner class is used only in this case.
 *
 * <p>2) As an instantiated regular class.
 *
 * <p>When used this way, the {@link create} method must be used to create an instance of this class
 * with a valid AVIF image. This will create a long running underlying decoder object which will be
 * used to decode the image(s). Using the returned object, other public methods of the class can be
 * called to get information about the image and to get the individual decoded frames. When the
 * decoder object is no longer needed, {@link release} must be called to release the underlying
 * decoder.
 *
 * <p>This is useful for decoding animated AVIF images and obtaining each decoded frame one after
 * the other.
 *
 * <p>NOTE: The API for using this as an instantiated regular class is still under development and
 * might change.
 */
@SuppressWarnings("CatchAndPrintStackTrace")
public class AvifDecoder {
  static {
    try {
      System.loadLibrary("avif_android");
    } catch (UnsatisfiedLinkError exception) {
      exception.printStackTrace();
    }
  }

  private long decoder;
  private int width;
  private int height;
  private int depth;
  private boolean alphaPresent;
  private int frameCount;
  private int repetitionCount;
  private double[] frameDurations;

  private AvifDecoder(ByteBuffer encoded, int threads) {
    decoder = createDecoder(encoded, encoded.remaining(), threads);
  }

  /** Contains information about the AVIF Image. This class is only used for getInfo(). */
  public static class Info {
    public int width;
    public int height;
    public int depth;
    public boolean alphaPresent;
  }

  /**
   * Returns true if the bytes in the buffer seem like an AVIF image.
   *
   * @param buffer The encoded image. buffer.position() must be 0.
   * @return true if the bytes seem like an AVIF image, false otherwise.
   */
  public static boolean isAvifImage(ByteBuffer buffer) {
    return AvifDecoder.isAvifImage(buffer, buffer.remaining());
  }

  private static native boolean isAvifImage(ByteBuffer encoded, int length);

  /**
   * Parses the AVIF header and populates the Info.
   *
   * @param encoded The encoded AVIF image. encoded.position() must be 0.
   * @param length Length of the encoded buffer.
   * @param info Output parameter whose fields will be populated.
   * @return true on success and false on failure.
   */
  public static native boolean getInfo(ByteBuffer encoded, int length, Info info);

  /**
   * Decodes the AVIF image into the bitmap.
   *
   * @param encoded The encoded AVIF image. encoded.position() must be 0.
   * @param length Length of the encoded buffer.
   * @param bitmap The decoded pixels will be copied into the bitmap.
   *     If the bitmap dimensions do not match the decoded image's dimensions,
   *               then the decoded image will be scaled to match the bitmap's dimensions.
   * @return true on success and false on failure. A few possible reasons for failure are: 1) Input
   *     was not valid AVIF.
   */
  public static boolean decode(ByteBuffer encoded, int length, Bitmap bitmap) {
    return decode(encoded, length, bitmap, 0);
  }

  /**
   * Decodes the AVIF image into the bitmap.
   *
   * @param encoded The encoded AVIF image. encoded.position() must be 0.
   * @param length Length of the encoded buffer.
   * @param bitmap The decoded pixels will be copied into the bitmap.
   *     If the bitmap dimensions do not match the decoded image's dimensions,
   *               then the decoded image will be scaled to match the bitmap's dimensions.
   * @param threads Number of threads to be used for the AVIF decode. Zero means use the library
   *     determined optimal value as the thread count. Negative values mean use the number of CPU
   *     cores as the thread count. For more details, see the documentation for maxThreads variable
   *     in avif.h.
   * @return true on success and false on failure.
   */
  public static native boolean decode(ByteBuffer encoded, int length, Bitmap bitmap, int threads);

  /** Get the width of the image. */
  public int getWidth() {
    return width;
  }

  /** Get the height of the image. */
  public int getHeight() {
    return height;
  }

  /** Get the depth (bit depth) of the image. */
  public int getDepth() {
    return depth;
  }

  /** Returns true if the image contains a transparency/alpha channel, false otherwise. */
  public boolean getAlphaPresent() {
    return alphaPresent;
  }

  /** Get the number of frames in the image. */
  public int getFrameCount() {
    return frameCount;
  }

  /**
   * Get the number of repetitions for an animated image (see repetitionCount in avif.h for
   * details).
   */
  public int getRepetitionCount() {
    return repetitionCount;
  }

  /** Get the duration for each frame in the image. */
  public double[] getFrameDurations() {
    return frameDurations;
  }

  /** Releases the underlying decoder object. */
  public void release() {
    if (decoder != 0) {
      destroyDecoder(decoder);
    }
    decoder = 0;
  }

  /**
   * Create and return an AvifDecoder.
   *
   * @param encoded The encoded AVIF image. encoded.position() must be 0. The memory of this
   *     ByteBuffer must be kept alive until release() is called.
   * @return null on failure. AvifDecoder object on success.
   */
  @Nullable
  public static AvifDecoder create(ByteBuffer encoded) {
    return create(encoded, /* threads= */ 1);
  }

  /**
   * Create and return an AvifDecoder with the specified number of threads.
   *
   * @param encoded The encoded AVIF image. encoded.position() must be 0. The memory of this
   *     ByteBuffer must be kept alive until release() is called.
   * @param threads Number of threads to be used by the decoder. Zero means use number of CPU cores
   *     as the thread count. Negative values are invalid. When this value is > 0, it is simply
   *     mapped to the maxThreads parameter in libavif. For more details, see the documentation for
   *     maxThreads variable in avif.h.
   * @return null on failure. AvifDecoder object on success.
   */
  @Nullable
  public static AvifDecoder create(ByteBuffer encoded, int threads) {
    AvifDecoder decoder = new AvifDecoder(encoded, threads);
    return (decoder.decoder == 0) ? null : decoder;
  }

  /**
   * Decodes the next frame of the animated AVIF into the bitmap.
   *
   * @param bitmap The decoded pixels will be copied into the bitmap.
   * @return 0 (AVIF_RESULT_OK) on success and some other avifResult on failure. For a list of all
   *     possible status codes, see the avifResult enum on avif.h in libavif's C source code. A
   *     String describing the return value can be obtained by calling {@link resultToString} with
   *     the return value of this function.
   */
  public int nextFrame(Bitmap bitmap) {
    return nextFrame(decoder, bitmap);
  }

  private native int nextFrame(long decoder, Bitmap bitmap);

  /**
   * Get the 0-based index of the frame that will be returned by the next call to {@link nextFrame}.
   * If the returned value is same as {@link getFrameCount}, then the next call to {@link nextFrame}
   * will fail.
   */
  public int nextFrameIndex() {
    return nextFrameIndex(decoder);
  }

  private native int nextFrameIndex(long decoder);

  /**
   * Decodes the nth frame of the animated AVIF into the bitmap.
   *
   * <p>Note that calling this method will change the behavior of subsequent calls to {@link
   * nextFrame}. {@link nextFrame} will start outputting the frame after this one.
   *
   * @param bitmap The decoded pixels will be copied into the bitmap.
   * @param n The zero-based index of the frame to be decoded.
   * @return 0 (AVIF_RESULT_OK) on success and some other avifResult on failure. For a list of all
   *     possible status codes, see the avifResult enum on avif.h in libavif's C source code. A
   *     String describing the return value can be obtained by calling {@link resultToString} with
   *     the return value of this function.
   */
  public int nthFrame(int n, Bitmap bitmap) {
    return nthFrame(decoder, n, bitmap);
  }

  private native int nthFrame(long decoder, int n, Bitmap bitmap);

  /**
   * Returns a String describing an avifResult enum value.
   *
   * @param result The avifResult value. Typically this is the return value of {@link nextFrame} or
   *     {@link nthFrame}.
   * @return A String containing the description of the avifResult.
   */
  public static native String resultToString(int result);

  /**
   * Returns a String that contains information about the libavif version, underlying codecs and
   * libyuv version (if available).
   */
  public static native String versionString();

  /**
   * Returns true if {@code display} supports high bit-depth (HDR) rendering.
   *
   * <p>Pass the result to {@link #decodeHardwareBitmap(ByteBuffer, int, int, boolean)} as
   * {@code allowHdr}: on SDR displays this avoids allocating an FP16 buffer; on HDR displays it
   * preserves the full colour range of >8-bit AVIF images.
   *
   * <p>Requires API 24; always returns false on older devices.
   *
   * @param display The display to query (typically {@code WindowManager.getDefaultDisplay()} or
   *     a display obtained from {@link DisplayManager}).
   * @return true if the display can render HDR content.
   */
  @RequiresApi(24)
  public static boolean isHighBitDepthDisplaySupported(Display display) {
    if (Build.VERSION.SDK_INT < 24) return false;
    if (Build.VERSION.SDK_INT >= 26 && display.isWideColorGamut()) return true;
    Display.HdrCapabilities caps = display.getHdrCapabilities();
    return caps != null && caps.getSupportedHdrTypes().length > 0;
  }

  /**
   * Decodes a still AVIF image and returns a hardware-backed {@link Bitmap} (Config.HARDWARE).
   *
   * <p>The returned Bitmap is GPU-resident and cannot be modified. Returns null if the device does
   * not support AHardwareBuffer (API < 26) or if the decode fails.
   *
   * @param encoded The encoded AVIF image. encoded.position() must be 0.
   * @param length Length of the encoded buffer.
   * @param threads Number of decode threads (0 = library default, negative = CPU core count).
   * @param allowHdr When true and the image has depth > 8, an R16G16B16A16_FLOAT (FP16) buffer is
   *     used to preserve HDR precision. When false, R8G8B8A8_UNORM is always used (SDR). Use
   *     {@link #isHighBitDepthDisplaySupported} to determine the right value.
   * @return A hardware-backed Bitmap on success, null on failure.
   */
  @RequiresApi(26)
  @Nullable
  public static Bitmap decodeHardwareBitmap(ByteBuffer encoded, int length, int threads,
      boolean allowHdr) {
    if (Build.VERSION.SDK_INT < 26) {
      return null;
    }
    return (Bitmap) nativeDecodeHardwareBitmap(encoded, length, threads, allowHdr);
  }

  /**
   * Decodes a still AVIF image and returns a hardware-backed {@link Bitmap} (Config.HARDWARE).
   *
   * <p>The returned Bitmap is GPU-resident and cannot be modified. Returns null if the device does
   * not support AHardwareBuffer (API < 26) or if the decode fails.
   *
   * <p>Always uses R8G8B8A8_UNORM (SDR). For HDR-aware decoding, use
   * {@link #decodeHardwareBitmap(ByteBuffer, int, int, boolean)}.
   *
   * @param encoded The encoded AVIF image. encoded.position() must be 0.
   * @param length Length of the encoded buffer.
   * @param threads Number of decode threads (0 = library default, negative = CPU core count).
   * @return A hardware-backed Bitmap on success, null on failure.
   */
  @RequiresApi(26)
  @Nullable
  public static Bitmap decodeHardwareBitmap(ByteBuffer encoded, int length, int threads) {
    return decodeHardwareBitmap(encoded, length, threads, /* allowHdr= */ false);
  }

  /**
   * Decodes a still AVIF image and returns a hardware-backed {@link Bitmap} (Config.HARDWARE).
   *
   * <p>Uses a single decode thread and R8G8B8A8_UNORM (SDR). Returns null on failure.
   *
   * @param encoded The encoded AVIF image. encoded.position() must be 0.
   * @param length Length of the encoded buffer.
   * @return A hardware-backed Bitmap on success, null on failure.
   */
  @RequiresApi(26)
  @Nullable
  public static Bitmap decodeHardwareBitmap(ByteBuffer encoded, int length) {
    return decodeHardwareBitmap(encoded, length, /* threads= */ 1, /* allowHdr= */ false);
  }

  /**
   * Allocates a {@link HardwareBuffer} compatible with this decoder's image for use with
   * {@link #nextFrameHardwareBitmap(boolean, HardwareBuffer)} across animation frames.
   *
   * <p>Reuse the same buffer each frame: a {@link Bitmap} wrapping it via
   * {@link Bitmap#wrapHardwareBuffer} reflects new content without re-allocation.
   * The caller is responsible for closing the buffer when done.
   *
   * @param allowHdr When true, prefer R16G16B16A16_FLOAT for >8-bit images (falls back to
   *     R8G8B8A8_UNORM if unsupported). When false, always uses R8G8B8A8_UNORM.
   * @return A new HardwareBuffer, or null on failure.
   */
  @RequiresApi(26)
  @Nullable
  public HardwareBuffer createHardwareBuffer(boolean allowHdr) {
    if (Build.VERSION.SDK_INT < 26) return null;
    return (HardwareBuffer) nativeCreateHardwareBuffer(width, height, depth, allowHdr);
  }

  /**
   * Decodes the next frame of an animated AVIF and returns a hardware-backed {@link Bitmap}.
   *
   * <p>If {@code dest} is non-null, decodes into that buffer and wraps it as a Bitmap — the
   * same Bitmap created via {@link Bitmap#wrapHardwareBuffer} reflects the new content without
   * re-allocation. If {@code dest} is null, a new {@link HardwareBuffer} is allocated internally.
   *
   * @param allowHdr When true and the image has depth > 8, FP16 is used. See
   *     {@link #decodeHardwareBitmap(ByteBuffer, int, int, boolean)}.
   * @param dest Optional pre-allocated buffer to decode into. Must match image dimensions.
   *     Use {@link #createHardwareBuffer} to allocate a compatible buffer.
   * @return A hardware-backed Bitmap on success, null on failure.
   */
  @RequiresApi(26)
  @Nullable
  public Bitmap nextFrameHardwareBitmap(boolean allowHdr, @Nullable HardwareBuffer dest) {
    if (Build.VERSION.SDK_INT < 26) return null;
    return (Bitmap) nativeNextFrameHardwareBitmap(decoder, allowHdr, dest);
  }

  /**
   * Decodes the next frame of an animated AVIF and returns a hardware-backed {@link Bitmap}.
   *
   * <p>Allocates a new {@link HardwareBuffer} internally on each call. For zero-copy frame
   * reuse, use {@link #nextFrameHardwareBitmap(boolean, HardwareBuffer)} instead.
   *
   * @param allowHdr When true and the image has depth > 8, FP16 is used.
   * @return A hardware-backed Bitmap on success, null on failure.
   */
  @RequiresApi(26)
  @Nullable
  public Bitmap nextFrameHardwareBitmap(boolean allowHdr) {
    return nextFrameHardwareBitmap(allowHdr, /* dest= */ null);
  }

  /**
   * Decodes the next frame of an animated AVIF and returns a hardware-backed {@link Bitmap}.
   *
   * <p>Uses R8G8B8A8_UNORM (SDR). Returns null on failure.
   *
   * @return A hardware-backed Bitmap on success, null on failure.
   */
  @RequiresApi(26)
  @Nullable
  public Bitmap nextFrameHardwareBitmap() {
    return nextFrameHardwareBitmap(/* allowHdr= */ false, /* dest= */ null);
  }

  /**
   * Decodes the nth frame of an animated AVIF and returns a hardware-backed {@link Bitmap}.
   *
   * <p>If {@code dest} is non-null, decodes into that buffer and wraps it as a Bitmap. If
   * {@code dest} is null, a new {@link HardwareBuffer} is allocated internally.
   *
   * @param n The zero-based index of the frame to decode.
   * @param allowHdr When true and the image has depth > 8, FP16 is used.
   * @param dest Optional pre-allocated buffer to decode into. Must match image dimensions.
   * @return A hardware-backed Bitmap on success, null on failure.
   */
  @RequiresApi(26)
  @Nullable
  public Bitmap nthFrameHardwareBitmap(int n, boolean allowHdr, @Nullable HardwareBuffer dest) {
    if (Build.VERSION.SDK_INT < 26) return null;
    return (Bitmap) nativeNthFrameHardwareBitmap(decoder, n, allowHdr, dest);
  }

  /**
   * Decodes the nth frame of an animated AVIF and returns a hardware-backed {@link Bitmap}.
   *
   * <p>Allocates a new {@link HardwareBuffer} internally. For zero-copy reuse, use
   * {@link #nthFrameHardwareBitmap(int, boolean, HardwareBuffer)} instead.
   *
   * @param n The zero-based index of the frame to decode.
   * @param allowHdr When true and the image has depth > 8, FP16 is used.
   * @return A hardware-backed Bitmap on success, null on failure.
   */
  @RequiresApi(26)
  @Nullable
  public Bitmap nthFrameHardwareBitmap(int n, boolean allowHdr) {
    return nthFrameHardwareBitmap(n, allowHdr, /* dest= */ null);
  }

  /**
   * Decodes the nth frame of an animated AVIF and returns a hardware-backed {@link Bitmap}.
   *
   * <p>Uses R8G8B8A8_UNORM (SDR). Returns null on failure.
   *
   * @param n The zero-based index of the frame to decode.
   * @return A hardware-backed Bitmap on success, null on failure.
   */
  @RequiresApi(26)
  @Nullable
  public Bitmap nthFrameHardwareBitmap(int n) {
    return nthFrameHardwareBitmap(n, /* allowHdr= */ false, /* dest= */ null);
  }

  private static native Object nativeDecodeHardwareBitmap(
      ByteBuffer encoded, int length, int threads, boolean allowHdr);

  private native Object nativeNextFrameHardwareBitmap(
      long decoder, boolean allowHdr, Object dest);

  private native Object nativeNthFrameHardwareBitmap(
      long decoder, int n, boolean allowHdr, Object dest);

  private native Object nativeCreateHardwareBuffer(
      int width, int height, int depth, boolean allowHdr);

  private native long createDecoder(ByteBuffer encoded, int length, int threads);

  private native void destroyDecoder(long decoder);
}
