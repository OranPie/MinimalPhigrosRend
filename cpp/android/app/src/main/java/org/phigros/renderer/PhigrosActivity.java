package org.phigros.renderer;

// Extends SDL2's Java Activity which handles the native lifecycle.
// SDL2 must be available — either as a vendored AAR or as source files
// copied from SDL2/android-project/app/src/main/java/org/libsdl/app/
import org.libsdl.app.SDLActivity;
import android.os.Bundle;
import android.content.ContentResolver;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.OpenableColumns;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

public class PhigrosActivity extends SDLActivity {

    private static final String TAG = "PhigrosRenderer";
    public static final String EXTRA_CHART_PATH = "org.phigros.renderer.chart_path";
    public static final String EXTRA_PLAY_MODE = "org.phigros.renderer.play_mode";
    public static final String PLAY_MODE_MANUAL = "manual";
    public static final String PLAY_MODE_SCORE_ONLY = "score_only";

    // Path that SDL_main will use; set before SDL starts.
    private String resolvedChartPath = null;
    private String resolvedPlayMode = PLAY_MODE_MANUAL;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // Resolve the chart path from the Intent before SDL starts.
        resolvedChartPath = resolveChartPath(getIntent());
        resolvedPlayMode = resolvePlayMode(getIntent());
        super.onCreate(savedInstanceState);
    }

    /** Returns the name of the C++ shared library to load. */
    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL2",
            "phigros_render"
        };
    }

    /**
     * Pass the native main arguments — chart path from Intent if present.
     * SDL_main() receives these as argv[].
     */
    @Override
    protected String[] getArguments() {
        final String chartPath = resolvedChartPath != null ? resolvedChartPath : "/sdcard/phigros/IN.json";
        final String modeArg = PLAY_MODE_SCORE_ONLY.equals(resolvedPlayMode) ? "--score-only" : "--play";
        if (resolvedChartPath != null) {
            return new String[]{ chartPath, modeArg };
        }
        return new String[]{ chartPath, modeArg };
    }

    /**
     * Resolve a chart file path from an Intent.
     *
     * Handles three URI schemes:
     *  1. file://  — direct filesystem path (older apps / file managers)
     *  2. content:// — Android 10+ file provider URIs; the file is copied to
     *                  the app's cache directory so the native layer can open it
     *                  with a plain filesystem path.
     *  3. No URI   — returns null, falling back to the default path in getArguments()
     */
    private String resolveChartPath(Intent intent) {
        if (intent == null) return null;
        String extraPath = intent.getStringExtra(EXTRA_CHART_PATH);
        if (extraPath != null && new File(extraPath).exists()) return extraPath;
        Uri data = intent.getData();
        if (data == null) return null;

        String scheme = data.getScheme();
        if (ContentResolver.SCHEME_FILE.equals(scheme)) {
            // file:// URI — extract the path directly
            String path = data.getPath();
            if (path != null && new File(path).exists()) return path;
        } else if (ContentResolver.SCHEME_CONTENT.equals(scheme)) {
            // content:// URI — copy file to cache and return that path
            return copyContentUriToCache(data);
        }
        return null;
    }

    private String resolvePlayMode(Intent intent) {
        if (intent == null) return PLAY_MODE_MANUAL;
        String requestedMode = intent.getStringExtra(EXTRA_PLAY_MODE);
        if (PLAY_MODE_SCORE_ONLY.equals(requestedMode)) return PLAY_MODE_SCORE_ONLY;
        return PLAY_MODE_MANUAL;
    }

    /**
     * Copies a content:// URI to the app's cache directory.
     * Returns the absolute path of the copied file, or null on failure.
     */
    private String copyContentUriToCache(Uri uri) {
        ContentResolver cr = getContentResolver();
        String displayName = queryDisplayName(cr, uri);

        // Determine file extension from the display name, defaulting to ".chart"
        // (avoid assuming .json since the file may also be .phbc)
        String ext = ".chart";
        if (displayName != null) {
            int dot = displayName.lastIndexOf('.');
            if (dot >= 0) ext = displayName.substring(dot).toLowerCase();
        }

        // Use a timestamp suffix to prevent collisions when multiple files are opened
        String baseName = "phigros_" + System.currentTimeMillis() + ext;

        // If we have a display name, sanitise it and prepend the timestamp prefix
        if (displayName != null) {
            // Sanitise to prevent path traversal
            String safeName = new File(displayName).getName();
            if (!safeName.isEmpty()) baseName = "phigros_" + System.currentTimeMillis() + "_" + safeName;
        }

        File outFile = new File(getCacheDir(), baseName);
        try (InputStream in = cr.openInputStream(uri);
             OutputStream out = new FileOutputStream(outFile)) {
            if (in == null) return null;
            byte[] buf = new byte[65536];
            int n;
            while ((n = in.read(buf)) != -1) out.write(buf, 0, n);
            Log.i(TAG, "Copied chart to: " + outFile.getAbsolutePath());
            return outFile.getAbsolutePath();
        } catch (IOException e) {
            Log.e(TAG, "Failed to copy chart: " + e.getMessage());
            return null;
        }
    }

    /** Returns the display name of a content URI, or null if unavailable. */
    private static String queryDisplayName(ContentResolver cr, Uri uri) {
        try (Cursor cursor = cr.query(uri, new String[]{ OpenableColumns.DISPLAY_NAME },
                null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int col = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (col >= 0) return cursor.getString(col);
            }
        } catch (Exception e) {
            Log.w(TAG, "Could not query display name: " + e.getMessage());
        }
        return null;
    }
}
