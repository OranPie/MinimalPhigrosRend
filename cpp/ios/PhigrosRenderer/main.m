// main.m — iOS entry point for PhigrosRenderer
// SDL2 on iOS uses UIApplicationMain with SDLUIKitDelegate.
// SDL_main (our C++ entry point) is called automatically once UIKit is ready.

#import <UIKit/UIKit.h>

// Forward-declare SDL2's UIKit application delegate
extern int SDL_main(int argc, char* argv[]);

// SDL2 provides SDLUIKitDelegate via SDL_uikit_main.c / SDL_uikit_delegate.m.
// We link against libSDL2 which supplies the UIApplicationDelegate and calls
// SDL_main() for us, so we only need to call UIApplicationMain here.
int main(int argc, char* argv[]) {
    @autoreleasepool {
        return UIApplicationMain(
            argc, argv,
            nil,
            NSStringFromClass([NSClassFromString(@"SDLUIKitDelegate") class])
        );
    }
}
