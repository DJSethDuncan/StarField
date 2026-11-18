#import <ScreenSaver/ScreenSaver.h>

/* Unique class name: every .saver shares one process, so names must not clash. */
@interface StarFieldScreenSaverView : ScreenSaverView
/* Most recent rendered frame (for the test harness). */
- (CGImageRef)currentFrame CF_RETURNS_NOT_RETAINED;
@end
