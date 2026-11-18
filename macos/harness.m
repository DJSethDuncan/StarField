/*
 * CI smoke test for StarField.saver: loads the built bundle the way the
 * screensaver host does, animates it for a few seconds and writes PNGs of the
 * rendered frames. Also exercises the configure sheet.
 *
 *   clang -fobjc-arc macos/harness.m -framework Cocoa -framework ScreenSaver \
 *     -o build/macos/harness && build/macos/harness build/macos/StarField.saver out/
 */
#import <Cocoa/Cocoa.h>
#import <ScreenSaver/ScreenSaver.h>

@interface ScreenSaverView (Frame)
- (CGImageRef)currentFrame;
@end

static BOOL WritePNG(CGImageRef image, NSString *path) {
    if (!image) return NO;
    NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithCGImage:image];
    NSData *png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    return [png writeToFile:path atomically:YES];
}

static int Brightness(CGImageRef image) {
    /* Count pixels noticeably brighter than the #020405 background. */
    size_t w = CGImageGetWidth(image), h = CGImageGetHeight(image);
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(NULL, w, h, 8, w * 4, space,
                                             kCGImageAlphaNoneSkipLast);
    CGColorSpaceRelease(space);
    CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), image);
    const uint8_t *px = CGBitmapContextGetData(ctx);
    int bright = 0;
    for (size_t i = 0; i < w * h; i++)
        if (px[i * 4] > 60) bright++;
    CGContextRelease(ctx);
    return bright;
}

static void Spin(double seconds) {
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:seconds]];
}

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        if (argc < 3) {
            fprintf(stderr, "usage: harness StarField.saver outdir\n");
            return 2;
        }
        [NSApplication sharedApplication];
        NSString *out = [NSString stringWithUTF8String:argv[2]];
        [[NSFileManager defaultManager] createDirectoryAtPath:out withIntermediateDirectories:YES
                                                   attributes:nil error:nil];
        NSBundle *bundle = [NSBundle bundleWithPath:[NSString stringWithUTF8String:argv[1]]];
        if (![bundle load]) {
            fprintf(stderr, "FAIL: could not load bundle\n");
            return 1;
        }
        Class cls = bundle.principalClass;
        if (![cls isSubclassOfClass:[ScreenSaverView class]]) {
            fprintf(stderr, "FAIL: principal class is not a ScreenSaverView\n");
            return 1;
        }

        NSRect frame = NSMakeRect(0, 0, 1280, 800);
        NSWindow *window = [[NSWindow alloc] initWithContentRect:frame
                                                       styleMask:NSWindowStyleMaskBorderless
                                                         backing:NSBackingStoreBuffered defer:NO];
        ScreenSaverView *view = [[cls alloc] initWithFrame:frame isPreview:NO];
        window.contentView = view;
        [window orderFront:nil];
        [view startAnimation];
        Spin(3.0);
        CGImageRef first = CGImageRetain([view currentFrame]);
        Spin(0.25);
        CGImageRef second = [view currentFrame];
        int failures = 0;
        if (!first || !second) {
            fprintf(stderr, "FAIL: no frames rendered\n");
            return 1;
        }
        int bright = Brightness(second);
        printf("frame %zux%zu, bright pixels: %d\n", CGImageGetWidth(second),
               CGImageGetHeight(second), bright);
        if (bright < 100) {
            fprintf(stderr, "FAIL: frame looks empty\n");
            failures++;
        }
        if (first == second) {
            fprintf(stderr, "FAIL: frame did not change while animating\n");
            failures++;
        }
        WritePNG(first, [out stringByAppendingPathComponent:@"macos-frame-1.png"]);
        WritePNG(second, [out stringByAppendingPathComponent:@"macos-frame-2.png"]);
        CGImageRelease(first);
        [view stopAnimation];

        /* Preview-sized instance, like System Settings' thumbnail. */
        ScreenSaverView *preview = [[cls alloc] initWithFrame:NSMakeRect(0, 0, 320, 200) isPreview:YES];
        [preview startAnimation];
        for (int i = 0; i < 30; i++) [preview animateOneFrame];
        WritePNG([preview currentFrame], [out stringByAppendingPathComponent:@"macos-preview.png"]);

        if (!view.hasConfigureSheet || !view.configureSheet) {
            fprintf(stderr, "FAIL: no configure sheet\n");
            failures++;
        } else {
            NSWindow *sheet = view.configureSheet;
            [sheet makeKeyAndOrderFront:nil];
            Spin(0.3);
            CGImageRef shot = CGWindowListCreateImage(CGRectNull, kCGWindowListOptionIncludingWindow,
                                                      (CGWindowID)sheet.windowNumber,
                                                      kCGWindowImageBoundsIgnoreFraming);
            if (shot) {
                WritePNG(shot, [out stringByAppendingPathComponent:@"macos-config.png"]);
                CGImageRelease(shot);
            }
            [sheet orderOut:nil];
        }
        printf(failures ? "FAILED\n" : "OK\n");
        return failures ? 1 : 0;
    }
}
