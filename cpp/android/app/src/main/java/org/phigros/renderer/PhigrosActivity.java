package org.phigros.renderer;

// Extends SDL2's Java Activity which handles the native lifecycle.
// SDL2 must be available — either as a vendored AAR or as source files
// copied from SDL2/android-project/app/src/main/java/org/libsdl/app/
import org.libsdl.app.SDLActivity;

public class PhigrosActivity extends SDLActivity {
    /** Returns the name of the C++ shared library to load. */
    @Override
    protected String[] getLibraries() {
        return new String[] {
            "phigros_sdl_app"
        };
    }

    @Override
    protected String[] getArguments() {
        return new String[]{};
    }
}
