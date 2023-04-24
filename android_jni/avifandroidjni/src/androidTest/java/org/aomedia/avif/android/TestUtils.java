package org.aomedia.avif.android;

import android.content.Context;
import androidx.test.platform.app.InstrumentationRegistry;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.channels.Channels;
import java.nio.file.Paths;

/** Utility class used by the instrumented tests. */
public class TestUtils {

  // Utility class. Cannot be instantiated.
  private TestUtils() {}

  public static class Image {
    public final String filename;
    public final int width;
    public final int height;
    public final int depth;
    public final boolean alphaPresent;
    int frameCount;
    int repetitionCount;
    double frameDuration;
    public final int threads;

    public Image(
        String filename, int width, int height, int depth, boolean alphaPresent, int threads) {
      this(
          filename,
          width,
          height,
          depth,
          alphaPresent,
          /* frameCount= */ 0,
          /* repetitionCount= */ 0,
          /* frameDuration= */ 0.0,
          threads);
    }

    public Image(
        String filename,
        int width,
        int height,
        int depth,
        boolean alphaPresent,
        int frameCount,
        int repetitionCount,
        double frameDuration,
        int threads) {
      this.filename = filename;
      this.width = width;
      this.height = height;
      this.depth = depth;
      this.alphaPresent = alphaPresent;
      this.frameCount = frameCount;
      this.repetitionCount = repetitionCount;
      this.frameDuration = frameDuration;
      this.threads = threads;
    }
  }

  public static ByteBuffer getBuffer(String assetDirectory, String filename) throws IOException {
    Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
    String assetPath = Paths.get(assetDirectory, filename).toString();
    InputStream is = context.getAssets().open(assetPath);
    ByteBuffer buffer = ByteBuffer.allocateDirect(is.available());
    Channels.newChannel(is).read(buffer);
    buffer.rewind();
    return buffer;
  }
}
