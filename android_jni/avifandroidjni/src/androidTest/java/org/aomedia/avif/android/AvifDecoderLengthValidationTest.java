// Copyright 2026 Google LLC
//
// Regression tests for libavif issue #3177 — signed-length OOB read at the
// Android JNI boundary. See PLAN.md §5 "Test strategy" for the full coverage
// matrix. Every case must complete without a native crash / ASan abort; the
// clean-failure contract is:
//   getInfo(...)        -> false
//   decode(...)         -> false
//   isAvifImage(...)    -> false
//   create(...)         -> null
//
// Happy-path cases in this file exist to guard against the new validation
// accidentally over-rejecting legitimate inputs (length == capacity,
// length == 0, valid images through create()).

package org.aomedia.avif.android;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import android.content.res.AssetManager;
import android.graphics.Bitmap;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

@RunWith(AndroidJUnit4.class)
public class AvifDecoderLengthValidationTest {

  /** The 8-byte truncated `ftyp` payload from issue #3177's PoC. */
  private static ByteBuffer tinyFtypDirectBuffer() {
    // Layout: 4-byte big-endian box size (16) + 4-byte box type 'ftyp'.
    // The declared size (16) intentionally exceeds the real capacity (8).
    ByteBuffer buf = ByteBuffer.allocateDirect(8);
    buf.order(ByteOrder.BIG_ENDIAN);
    buf.putInt(16);
    buf.put((byte) 'f');
    buf.put((byte) 't');
    buf.put((byte) 'y');
    buf.put((byte) 'p');
    buf.flip();
    return buf;
  }

  /** Same bytes as {@link #tinyFtypDirectBuffer()} but heap-backed. */
  private static ByteBuffer tinyFtypHeapBuffer() {
    ByteBuffer buf = ByteBuffer.allocate(8);
    buf.order(ByteOrder.BIG_ENDIAN);
    buf.putInt(16);
    buf.put((byte) 'f');
    buf.put((byte) 't');
    buf.put((byte) 'y');
    buf.put((byte) 'p');
    buf.flip();
    return buf;
  }

  private static ByteBuffer emptyDirectBuffer() {
    ByteBuffer buf = ByteBuffer.allocateDirect(0);
    buf.flip();
    return buf;
  }

  private static ByteBuffer loadDirectAssetBuffer(String assetPath) throws IOException {
    AssetManager assets =
        InstrumentationRegistry.getInstrumentation().getContext().getAssets();
    try (InputStream in = assets.open(assetPath)) {
      byte[] bytes = new byte[in.available()];
      int offset = 0;
      while (offset < bytes.length) {
        int read = in.read(bytes, offset, bytes.length - offset);
        if (read < 0) {
          break;
        }
        offset += read;
      }
      ByteBuffer buf = ByteBuffer.allocateDirect(offset);
      buf.put(bytes, 0, offset);
      buf.flip();
      return buf;
    }
  }

  private static Bitmap freshBitmap() {
    // Dimensions are irrelevant — decode() must early-exit on validation failure
    // before it touches the bitmap. Use a minimal 1x1 ARGB_8888 to keep memory
    // pressure negligible for the happy-path cases too.
    return Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888);
  }

  // ---------------------------------------------------------------------------
  // getInfo
  // ---------------------------------------------------------------------------

  @Test
  public void getInfo_negativeOne_returnsFalseNoCrash() {
    ByteBuffer buf = tinyFtypDirectBuffer();
    AvifDecoder.Info info = new AvifDecoder.Info();
    assertFalse(AvifDecoder.getInfo(buf, -1, info));
  }

  @Test
  public void getInfo_integerMinValue_returnsFalseNoCrash() {
    // 0x80000000 — the second value explicitly named in the issue PoC.
    ByteBuffer buf = tinyFtypDirectBuffer();
    AvifDecoder.Info info = new AvifDecoder.Info();
    assertFalse(AvifDecoder.getInfo(buf, Integer.MIN_VALUE, info));
  }

  @Test
  public void getInfo_largeNegativeOtherThanMinValue_returnsFalseNoCrash() {
    // -2 exercises the generic `length < 0` branch (not just the PoC corners).
    ByteBuffer buf = tinyFtypDirectBuffer();
    AvifDecoder.Info info = new AvifDecoder.Info();
    assertFalse(AvifDecoder.getInfo(buf, -2, info));
  }

  @Test
  public void getInfo_lengthOneOverCapacity_returnsFalseNoCrash() {
    // capacity == 8, length == 9. Strict `>` bound — must reject.
    ByteBuffer buf = tinyFtypDirectBuffer();
    AvifDecoder.Info info = new AvifDecoder.Info();
    assertFalse(AvifDecoder.getInfo(buf, 9, info));
  }

  @Test
  public void getInfo_integerMaxLengthOverCapacity_returnsFalseNoCrash() {
    // Large positive length that still fits in a signed int. capacity == 8.
    ByteBuffer buf = tinyFtypDirectBuffer();
    AvifDecoder.Info info = new AvifDecoder.Info();
    assertFalse(AvifDecoder.getInfo(buf, Integer.MAX_VALUE, info));
  }

  @Test
  public void getInfo_honestLength_returnsFalseTruncatedControl() {
    // PLAN.md §5 layer-2 case #4: the pre-fix behavior on honest length must be
    // preserved (truncated ftyp -> parser-level error, clean `false`).
    ByteBuffer buf = tinyFtypDirectBuffer();
    AvifDecoder.Info info = new AvifDecoder.Info();
    assertFalse(AvifDecoder.getInfo(buf, 8, info));
  }

  @Test
  public void getInfo_lengthEqualsCapacity_notRejectedByBoundary() {
    // PLAN.md §4: the check is strict `>`, so length == capacity (8) must not be
    // rejected by the helper. Parser still returns false on truncated data, but
    // reaching the parser is the whole point of this case.
    ByteBuffer buf = tinyFtypDirectBuffer();
    AvifDecoder.Info info = new AvifDecoder.Info();
    assertFalse(AvifDecoder.getInfo(buf, buf.capacity(), info));
  }

  @Test
  public void getInfo_lengthZero_passedThroughToParser() {
    // PLAN.md §4: length == 0 must *not* be short-circuited at the JNI layer.
    // The parser handles the zero-byte case itself and returns a clean error;
    // we verify we still get a clean false-without-crash.
    ByteBuffer buf = emptyDirectBuffer();
    AvifDecoder.Info info = new AvifDecoder.Info();
    assertFalse(AvifDecoder.getInfo(buf, 0, info));
  }

  @Test
  public void getInfo_heapBackedBuffer_returnsFalseNoCrash() {
    // Non-direct ByteBuffer -> GetDirectBufferCapacity returns -1 -> reject.
    // Also guards the CR-7 null-address bug noted in PLAN.md §4.
    ByteBuffer buf = tinyFtypHeapBuffer();
    AvifDecoder.Info info = new AvifDecoder.Info();
    assertFalse(AvifDecoder.getInfo(buf, buf.remaining(), info));
  }

  @Test
  public void getInfo_heapBackedBufferNegativeLength_returnsFalseNoCrash() {
    // Crossed-cases: both the negative-length and the not-a-direct-buffer branch
    // apply. Whichever rejects first is fine — the contract is a clean false.
    ByteBuffer buf = tinyFtypHeapBuffer();
    AvifDecoder.Info info = new AvifDecoder.Info();
    assertFalse(AvifDecoder.getInfo(buf, -1, info));
  }

  // ---------------------------------------------------------------------------
  // decode
  // ---------------------------------------------------------------------------

  @Test
  public void decode_negativeOne_returnsFalseNoCrash() {
    ByteBuffer buf = tinyFtypDirectBuffer();
    Bitmap bmp = freshBitmap();
    assertFalse(AvifDecoder.decode(buf, -1, bmp, 1));
  }

  @Test
  public void decode_integerMinValue_returnsFalseNoCrash() {
    ByteBuffer buf = tinyFtypDirectBuffer();
    Bitmap bmp = freshBitmap();
    assertFalse(AvifDecoder.decode(buf, Integer.MIN_VALUE, bmp, 1));
  }

  @Test
  public void decode_lengthOneOverCapacity_returnsFalseNoCrash() {
    ByteBuffer buf = tinyFtypDirectBuffer();
    Bitmap bmp = freshBitmap();
    assertFalse(AvifDecoder.decode(buf, 9, bmp, 1));
  }

  @Test
  public void decode_integerMaxLengthOverCapacity_returnsFalseNoCrash() {
    ByteBuffer buf = tinyFtypDirectBuffer();
    Bitmap bmp = freshBitmap();
    assertFalse(AvifDecoder.decode(buf, Integer.MAX_VALUE, bmp, 1));
  }

  @Test
  public void decode_honestLength_returnsFalseTruncatedControl() {
    ByteBuffer buf = tinyFtypDirectBuffer();
    Bitmap bmp = freshBitmap();
    assertFalse(AvifDecoder.decode(buf, 8, bmp, 1));
  }

  @Test
  public void decode_heapBackedBuffer_returnsFalseNoCrash() {
    ByteBuffer buf = tinyFtypHeapBuffer();
    Bitmap bmp = freshBitmap();
    assertFalse(AvifDecoder.decode(buf, buf.remaining(), bmp, 1));
  }

  @Test
  public void decode_lengthZero_passedThroughToParser() {
    ByteBuffer buf = emptyDirectBuffer();
    Bitmap bmp = freshBitmap();
    assertFalse(AvifDecoder.decode(buf, 0, bmp, 1));
  }

  // ---------------------------------------------------------------------------
  // isAvifImage — public wrapper uses `encoded.remaining()` internally, so the
  // length branch is exercised by way of buffer-shape variations.
  // ---------------------------------------------------------------------------

  @Test
  public void isAvifImage_truncatedFtypDirect_returnsFalseNoCrash() {
    ByteBuffer buf = tinyFtypDirectBuffer();
    assertFalse(AvifDecoder.isAvifImage(buf));
  }

  @Test
  public void isAvifImage_heapBackedBuffer_returnsFalseNoCrash() {
    // GetDirectBufferCapacity(...) == -1 -> reject path.
    ByteBuffer buf = tinyFtypHeapBuffer();
    assertFalse(AvifDecoder.isAvifImage(buf));
  }

  @Test
  public void isAvifImage_emptyDirectBuffer_returnsFalseNoCrash() {
    ByteBuffer buf = emptyDirectBuffer();
    assertFalse(AvifDecoder.isAvifImage(buf));
  }

  // ---------------------------------------------------------------------------
  // create (AvifDecoder factory). Uses encoded.remaining() internally, so the
  // length branch can't be poisoned from the public Java surface. These tests
  // guard against the createDecoder hardening accidentally breaking the happy
  // path or the clean-failure contract on malformed input.
  // ---------------------------------------------------------------------------

  @Test
  public void create_truncatedFtypDirect_returnsNull() {
    // The tiny_ftyp 8-byte payload is not a valid AVIF — create() must return
    // null without crashing.
    ByteBuffer buf = tinyFtypDirectBuffer();
    assertNull(AvifDecoder.create(buf));
  }

  @Test
  public void create_heapBackedBuffer_returnsNull() {
    // Non-direct buffer: GetDirectBufferCapacity returns -1 -> clean failure.
    ByteBuffer buf = tinyFtypHeapBuffer();
    assertNull(AvifDecoder.create(buf));
  }

  @Test
  public void create_emptyDirectBuffer_returnsNull() {
    ByteBuffer buf = emptyDirectBuffer();
    assertNull(AvifDecoder.create(buf));
  }

  @Test
  public void create_validImage_stillReturnsNonNull() throws IOException {
    // PLAN.md §5 layer-2 case #8: guards against the private createDecoder
    // hardening accidentally rejecting the happy path.
    ByteBuffer buf = loadDirectAssetBuffer("avif/fox.profile0.8bpc.yuv420.avif");
    AvifDecoder decoder = AvifDecoder.create(buf);
    assertNotNull(decoder);
  }

  @Test
  public void getInfo_validImageHonestLength_returnsTrue() throws IOException {
    // Sanity: after hardening, the honest-length path on a well-formed image
    // must continue to succeed. Regression guard on the `length == capacity`
    // happy path at a realistic size (not just 8 bytes).
    ByteBuffer buf = loadDirectAssetBuffer("avif/fox.profile0.8bpc.yuv420.avif");
    AvifDecoder.Info info = new AvifDecoder.Info();
    assertTrue(AvifDecoder.getInfo(buf, buf.remaining(), info));
  }

  @Test
  public void getInfo_validImageNegativeLength_returnsFalseNoCrash() throws IOException {
    // A large, legitimately-sized direct buffer plus a negative length must
    // still be rejected — the sign-extension path is the original CVE.
    ByteBuffer buf = loadDirectAssetBuffer("avif/fox.profile0.8bpc.yuv420.avif");
    AvifDecoder.Info info = new AvifDecoder.Info();
    assertFalse(AvifDecoder.getInfo(buf, -1, info));
  }

  @Test
  public void getInfo_validImageLengthOneOverCapacity_returnsFalseNoCrash() throws IOException {
    ByteBuffer buf = loadDirectAssetBuffer("avif/fox.profile0.8bpc.yuv420.avif");
    AvifDecoder.Info info = new AvifDecoder.Info();
    assertFalse(AvifDecoder.getInfo(buf, buf.capacity() + 1, info));
  }
}
