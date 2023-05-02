package org.aomedia.avif.android;

import static com.google.common.truth.Truth.assertThat;

import android.graphics.Bitmap;
import android.graphics.Bitmap.Config;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;
import org.aomedia.avif.android.AvifDecoder.Info;
import org.aomedia.avif.android.TestUtils.Image;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;
import org.junit.runners.Parameterized.Parameter;
import org.junit.runners.Parameterized.Parameters;

/** Instrumentation tests for the libavif JNI API, which will execute on an Android device. */
@RunWith(Parameterized.class)
public class StillImageTest {

  private static final Image[] IMAGES = {
    // Parameter ordering: filename, width, height, depth, alphaPresent, threads.
    new Image("fox.profile0.10bpc.yuv420.avif", 1204, 800, 10, false, 1),
    new Image("fox.profile0.10bpc.yuv420.monochrome.avif", 1204, 800, 10, false, 1),
    new Image("fox.profile0.8bpc.yuv420.avif", 1204, 800, 8, false, 1),
    new Image("fox.profile0.8bpc.yuv420.monochrome.avif", 1204, 800, 8, false, 1),
    new Image("fox.profile1.10bpc.yuv444.avif", 1204, 800, 10, false, 1),
    new Image("fox.profile1.8bpc.yuv444.avif", 1204, 800, 8, false, 1),
    new Image("fox.profile2.10bpc.yuv422.avif", 1204, 800, 10, false, 1),
    new Image("fox.profile2.12bpc.yuv420.avif", 1204, 800, 12, false, 1),
    new Image("fox.profile2.12bpc.yuv420.monochrome.avif", 1204, 800, 12, false, 1),
    new Image("fox.profile2.12bpc.yuv422.avif", 1204, 800, 12, false, 1),
    new Image("fox.profile2.12bpc.yuv444.avif", 1204, 800, 12, false, 1),
    new Image("fox.profile2.8bpc.yuv422.avif", 1204, 800, 8, false, 1)
  };

  private static final String ASSET_DIRECTORY = "avif";

  @Parameters
  public static List<Object[]> data() throws IOException {
    ArrayList<Object[]> list = new ArrayList<>();
    for (Image image : IMAGES) {
      // Test ARGB_8888 for all files.
      list.add(new Object[] {Config.ARGB_8888, image});
      // For 8bpc files, test only RGB_565. For other files, test only RGBA_F16.
      Config testConfig = (image.depth == 8) ? Config.RGB_565 : Config.RGBA_F16;
      list.add(new Object[] {testConfig, image});
    }
    return list;
  }

  @Parameter(0)
  public Bitmap.Config config;

  @Parameter(1)
  public Image image;

  @Test
  public void testAvifDecode() throws IOException {
    ByteBuffer buffer = TestUtils.getBuffer(ASSET_DIRECTORY, image.filename);
    assertThat(buffer).isNotNull();
    assertThat(AvifDecoder.isAvifImage(buffer)).isTrue();
    Info info = new Info();
    assertThat(AvifDecoder.getInfo(buffer, buffer.remaining(), info)).isTrue();
    assertThat(info.width).isEqualTo(image.width);
    assertThat(info.height).isEqualTo(image.height);
    assertThat(info.depth).isEqualTo(image.depth);
    Bitmap bitmap = Bitmap.createBitmap(info.width, info.height, config);
    assertThat(bitmap).isNotNull();
    assertThat(AvifDecoder.decode(buffer, buffer.remaining(), bitmap)).isTrue();
  }
}
