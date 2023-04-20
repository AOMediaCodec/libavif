package org.aomedia.avif.android;

import static com.google.common.truth.Truth.assertThat;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Bitmap.Config;
import androidx.test.platform.app.InstrumentationRegistry;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.channels.Channels;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;
import org.junit.runners.Parameterized.Parameter;
import org.junit.runners.Parameterized.Parameters;

/** Instrumentation tests for the libavif JNI API, which will execute on an Android device. */
@RunWith(Parameterized.class)
public class AnimatedImageTest {

  private static class Image {
    public final String filename;
    public final int width;
    public final int height;
    public final int depth;
    public final int frameCount;
    public final int repetitionCount;
    public final double frameDuration;

    public Image(
        String filename,
        int width,
        int height,
        int depth,
        int frameCount,
        int repetitionCount,
        double frameDuration) {
      this.filename = filename;
      this.width = width;
      this.height = height;
      this.depth = depth;
      this.frameCount = frameCount;
      this.repetitionCount = repetitionCount;
      this.frameDuration = frameDuration;
    }
  }

  private static final Image[] IMAGES = {
    // Parameter ordering: filename, width, height, depth, frameCount, repetitionCount,
    // frameDuration.
    new Image("alpha_video.avif", 640, 480, 8, 48, -2, 0.04),
    new Image("Chimera-AV1-10bit-480x270.avif", 480, 270, 10, 95, -2, 0.04),
  };

  private static final String ASSET_DIRECTORY = "animated_avif";

  private static final Bitmap.Config[] BITMAP_CONFIGS = {
    Config.ARGB_8888, Config.RGBA_F16, Config.RGB_565,
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
    ByteBuffer buffer = getBuffer();
    assertThat(buffer).isNotNull();
    AvifDecoder decoder = AvifDecoder.create(buffer);
    assertThat(decoder).isNotNull();
    assertThat(decoder.getWidth()).isEqualTo(image.width);
    assertThat(decoder.getHeight()).isEqualTo(image.height);
    assertThat(decoder.getDepth()).isEqualTo(image.depth);
    assertThat(decoder.getFrameCount()).isEqualTo(image.frameCount);
    assertThat(decoder.getRepetitionCount()).isEqualTo(image.repetitionCount);
    double[] frameDurations = decoder.getFrameDurations();
    assertThat(frameDurations).isNotNull();
    assertThat(frameDurations).hasLength(image.frameCount);
    Bitmap bitmap = Bitmap.createBitmap(image.width, image.height, config);
    assertThat(bitmap).isNotNull();
    for (int i = 0; i < image.frameCount; i++) {
      assertThat(decoder.nextFrame(bitmap)).isTrue();
      assertThat(frameDurations[i]).isWithin(1.0e-2).of(image.frameDuration);
    }
    decoder.release();
  }

  private ByteBuffer getBuffer() throws IOException {
    Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
    String assetPath = Paths.get(ASSET_DIRECTORY, image.filename).toString();
    InputStream is = context.getAssets().open(assetPath);
    ByteBuffer buffer = ByteBuffer.allocateDirect(is.available());
    Channels.newChannel(is).read(buffer);
    buffer.rewind();
    return buffer;
  }
}
