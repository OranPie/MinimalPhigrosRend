// PhigrosAppDelegate.m — Application delegate for PhigrosRenderer iOS
// SDL2's SDLUIKitDelegate handles the actual SDL lifecycle; this subclass
// adds chart-file-opening support via the document picker / file association.

#import "PhigrosAppDelegate.h"
#import <UIKit/UIKit.h>
#import <SDL2/SDL.h>

@implementation PhigrosAppDelegate

// ── File-open intent (called when the user opens a .json/.phbc chart) ─────────
- (BOOL)application:(UIApplication *)application
            openURL:(NSURL *)url
            options:(NSDictionary<UIApplicationOpenURLOptionsKey,id> *)options
{
    NSString *path = [url path];
    if (!path) return NO;

    // Store the chart path so PhigrosViewController can pick it up when
    // SDL calls SDL_main. If the engine is already running (background case),
    // post a notification that the game loop can listen for.
    [[NSUserDefaults standardUserDefaults] setObject:path forKey:@"phigros_open_chart"];
    [[NSUserDefaults standardUserDefaults] synchronize];

    [[NSNotificationCenter defaultCenter]
        postNotificationName:@"PhigrosOpenChart"
                      object:path];
    return YES;
}

// ── Scene support (iOS 13+) ───────────────────────────────────────────────────
- (UISceneConfiguration *)application:(UIApplication *)application
    configurationForConnectingSceneSession:(UISceneSession *)connectingSceneSession
                                   options:(UISceneConnectionOptions *)options
    API_AVAILABLE(ios(13.0))
{
    return [[UISceneConfiguration alloc]
        initWithName:@"Default Configuration"
        sessionRole:connectingSceneSession.role];
}

@end
