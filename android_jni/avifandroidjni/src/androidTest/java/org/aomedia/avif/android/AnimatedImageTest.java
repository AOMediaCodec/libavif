package org.aomedia.avif.android;

import static com.google.common.truth.Truth.assertThat;

import android.graphics.Bitmap;
import android.graphics.Bitmap.Config;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;
import org.aomedia.avif.android.TestUtils.Image;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;
import org.junit.runners.Parameterized.Parameter;
import org.junit.runners.Parameterized.Parameters;

/** Instrumentation tests for the libavif JNI API, which will execute on an Android device. */
@RunWith(Parameterized.class)
public class AnimatedImageTest {

  private static final int AVIF_RESULT_OK = 0;

  private static final Image[] IMAGES = {
    // Parameter ordering: filename, width, height, depth, alphaPresent, frameCount,
    // repetitionCount, frameDuration, threads.
    new Image("alpha_video.avif", 640, 480, 8, true, 48, -2, 0.04, 1),
    new Image("Chimera-AV1-10bit-480x270.avif", 480, 270, 10, false, 95, -2, 0.04, 2),
  };

  private static final String ASSET_DIRECTORY = "animated_avif";

  private static final Bitmap.Config[] BITMAP_CONFIGS = {
    // RGBA_F16 is not tested because x86 emulators are flaky with that.
    Config.ARGB_8888, Config.RGB_565,
  };

  @Parameters
  public static List<Object[]> data() throws IOException {
    ArrayList<Object[]> list = new ArrayList<>();
    for (Bitmap.Config config : BITMAP_CONFIGS) {
      for (Image image : IMAGES) {
        list.add(new Object[] {config, image});
      }
    }
    return list;
  }

  @Parameter(0)
  public Bitmap.Config config;

  @Parameter(1)
  public Image image;

  @Test
  public void testAnimatedAvifDecode() throws IOException {
    ByteBuffer buffer = TestUtils.getBuffer(ASSET_DIRECTORY, image.filename);
    assertThat(buffer).isNotNull();
    AvifDecoder decoder = AvifDecoder.create(buffer, image.threads);
    assertThat(decoder).isNotNull();
    assertThat(decoder.getWidth()).isEqualTo(image.width);
    assertThat(decoder.getHeight()).isEqualTo(image.height);
    assertThat(decoder.getDepth()).isEqualTo(image.depth);
    assertThat(decoder.getAlphaPresent()).isEqualTo(image.alphaPresent);
    assertThat(decoder.getFrameCount()).isEqualTo(image.frameCount);
    assertThat(decoder.getRepetitionCount()).isEqualTo(image.repetitionCount);
    double[] frameDurations = decoder.getFrameDurations();
    assertThat(frameDurations).isNotNull();
    assertThat(frameDurations).hasLength(image.frameCount);
    Bitmap bitmap = Bitmap.createBitmap(image.width, image.height, config);
    assertThat(bitmap).isNotNull();
    for (int i = 0; i < image.frameCount; i++) {
      assertThat(decoder.nextFrameIndex()).isEqualTo(i);
      assertThat(decoder.nextFrame(bitmap)).isEqualTo(AVIF_RESULT_OK);
      assertThat(frameDurations[i]).isWithin(1.0e-2).of(image.frameDuration);
    }
    assertThat(decoder.nextFrameIndex()).isEqualTo(image.frameCount);
    // Fetch the first frame again.
    assertThat(decoder.nthFrame(0, bitmap)).isEqualTo(AVIF_RESULT_OK);
    // Now nextFrame will return the second frame.
    assertThat(decoder.nextFrameIndex()).isEqualTo(1);
    assertThat(decoder.nextFrame(bitmap)).isEqualTo(AVIF_RESULT_OK);
    // Fetch the (frameCount/2)th frame.
    assertThat(decoder.nthFrame(image.frameCount / 2, bitmap)).isEqualTo(AVIF_RESULT_OK);
    // Fetch the last frame.
    assertThat(decoder.nthFrame(image.frameCount - 1, bitmap)).isEqualTo(AVIF_RESULT_OK);
    // Now nextFrame should return false.
    assertThat(decoder.nextFrameIndex()).isEqualTo(image.frameCount);
    assertThat(decoder.nextFrame(bitmap)).isNotEqualTo(AVIF_RESULT_OK);
    // Passing out of bound values for n should fail.
    assertThat(decoder.nthFrame(-1, bitmap)).isNotEqualTo(AVIF_RESULT_OK);
    assertThat(decoder.nthFrame(image.frameCount, bitmap)).isNotEqualTo(AVIF_RESULT_OK);
    decoder.release();
  }

  @Test
  public void testUtilityFunctions() throws IOException {
    // Test the avifResult value whose value and string representations are least likely to change.
    assertThat(AvifDecoder.resultToString(AVIF_RESULT_OK)).isEqualTo("OK");
    // Ensure that the version string starts with "libavif".
    assertThat(AvifDecoder.versionString()).startsWith("libavif");
  }
}
