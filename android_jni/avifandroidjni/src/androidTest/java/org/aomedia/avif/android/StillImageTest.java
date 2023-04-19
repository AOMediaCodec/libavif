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
import java.util.Arrays;
import java.util.List;
import org.aomedia.avif.android.AvifDecoder.Info;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;
import org.junit.runners.Parameterized.Parameter;
import org.junit.runners.Parameterized.Parameters;

/** Instrumentation tests for the libavif JNI API, which will execute on an Android device. */
@RunWith(Parameterized.class)
public class StillImageTest {

  private static final String ASSET_DIRECTORY = "avif";

  @Parameters
  public static List<Object[]> data() throws IOException {
    ArrayList<Object[]> list = new ArrayList<>();
    for (String asset : getAssetFiles(ASSET_DIRECTORY)) {
      String assetPath = Paths.get(ASSET_DIRECTORY, asset).toString();
      // Test ARGB_8888 for all files.
      list.add(new Object[] {Config.ARGB_8888, assetPath});
      // For 8bpc files, test only RGB_565. For other files, test only RGBA_F16.
      Config testConfig = assetPath.contains("8bpc") ? Config.RGB_565 : Config.RGBA_F16;
      list.add(new Object[] {testConfig, assetPath});
    }
    return list;
  }

  @Parameter(0)
  public Bitmap.Config config;

  @Parameter(1)
  public String assetPath;

  @Test
  public void testIsAvifImageReturnsTrue() throws IOException {
    ByteBuffer buffer = getBuffer();
    assertThat(buffer).isNotNull();
    assertThat(AvifDecoder.isAvifImage(buffer)).isTrue();
  }

  @Test
  public void testAvifDecode() throws IOException {
    ByteBuffer buffer = getBuffer();
    assertThat(buffer).isNotNull();
    Info info = new Info();
    assertThat(AvifDecoder.getInfo(buffer, buffer.remaining(), info)).isTrue();
    assertThat(info.width).isGreaterThan(0);
    assertThat(info.height).isGreaterThan(0);
    assertThat(info.depth).isAnyOf(8, 10, 12);
    Bitmap bitmap = Bitmap.createBitmap(info.width, info.height, config);
    assertThat(bitmap).isNotNull();
    assertThat(AvifDecoder.decode(buffer, buffer.remaining(), bitmap)).isTrue();
  }

  private ByteBuffer getBuffer() throws IOException {
    Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
    InputStream is = context.getAssets().open(assetPath);
    ByteBuffer buffer = ByteBuffer.allocateDirect(is.available());
    Channels.newChannel(is).read(buffer);
    buffer.rewind();
    return buffer;
  }

  private static List<String> getAssetFiles(String directoryName) throws IOException {
    Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
    return Arrays.asList(context.getAssets().list(directoryName));
  }
}
