package org.phigros.renderer;

// Extends SDL2's Java Activity which handles the native lifecycle.
// SDL2 must be available — either as a vendored AAR or as source files
// copied from SDL2/android-project/app/src/main/java/org/libsdl/app/
import org.libsdl.app.SDLActivity;
import android.os.Bundle;
import android.content.Intent;
import android.net.Uri;

public class PhigrosActivity extends SDLActivity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
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
        Intent intent = getIntent();
        Uri data = (intent != null) ? intent.getData() : null;
        if (data != null) {
            String path = data.getPath();
            if (path != null) {
                return new String[]{ path, "--score-only" };
            }
        }
        // Default: score-only mode on the bundled chart (assets/IN.json if present)
        return new String[]{ "/sdcard/phigros/IN.json", "--score-only" };
    }
}
