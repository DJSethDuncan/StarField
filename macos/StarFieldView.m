/*
 * StarField for macOS: a native ScreenSaverView.
 *
 * Renders the C core's draw list into a CGBitmapContext sized like the web
 * canvas (logical size x min(DPR, 2), capped at 8M pixels) and hands the
 * finished frame to the view's layer. One view is created per display.
 */
#import "StarFieldView.h"

#import <QuartzCore/QuartzCore.h>
#include <mach/mach_time.h>
#include <math.h>

#include "starfield.h"

static NSString *const kSpeed = @"Speed";
static NSString *const kDensity = @"Density";
static NSString *const kTailLength = @"TailLength";
static NSString *const kVariation = @"Variation";
static NSString *const kPhaseShift = @"PhaseShift";
static NSString *const kMotion = @"Motion";

static ScreenSaverDefaults *StarFieldDefaults(void) {
    NSString *identifier =
        [[NSBundle bundleForClass:[StarFieldScreenSaverView class]] bundleIdentifier]
            ?: @"com.djsethduncan.StarField";
    ScreenSaverDefaults *defaults = [ScreenSaverDefaults defaultsForModuleWithName:identifier];
    [defaults registerDefaults:@{
        kSpeed : @1.0, kDensity : @1.0, kTailLength : @1.0, kVariation : @0.0,
        kPhaseShift : @NO, kMotion : @YES
    }];
    return defaults;
}

static sf_settings LoadSettings(void) {
    ScreenSaverDefaults *d = StarFieldDefaults();
    sf_settings s;
    s.speed = [d doubleForKey:kSpeed];
    s.density = [d doubleForKey:kDensity];
    s.tail_length = [d doubleForKey:kTailLength];
    s.variation = [d doubleForKey:kVariation];
    s.phase_shift = [d boolForKey:kPhaseShift];
    s.motion = [d boolForKey:kMotion];
    sf_settings_sanitize(&s);
    return s;
}

#pragma mark - Configure sheet

@interface StarFieldConfigController : NSObject
@property(nonatomic, strong) NSWindow *window;
- (void)applySettings:(sf_settings)settings;
@end

@implementation StarFieldConfigController {
    NSButton *_motion, *_phaseShift;
    NSSlider *_speed, *_density, *_variation, *_tail;
    NSTextField *_speedLabel, *_densityLabel, *_variationLabel, *_tailLabel;
}

- (NSTextField *)label:(NSString *)text {
    NSTextField *label = [NSTextField labelWithString:text];
    label.translatesAutoresizingMaskIntoConstraints = NO;
    return label;
}

- (NSSlider *)sliderMin:(double)min max:(double)max step:(double)step {
    NSSlider *slider = [NSSlider sliderWithValue:min minValue:min maxValue:max
                                          target:self action:@selector(changed:)];
    /* Snap to the same steps as the web panel (see -changed:). */
    slider.tag = (NSInteger)llround(step * 1000);
    slider.translatesAutoresizingMaskIntoConstraints = NO;
    [slider.widthAnchor constraintEqualToConstant:320].active = YES;
    return slider;
}

- (NSButton *)check:(NSString *)title {
    NSButton *box = [NSButton checkboxWithTitle:title target:self action:@selector(changed:)];
    box.translatesAutoresizingMaskIntoConstraints = NO;
    return box;
}

- (NSButton *)button:(NSString *)title action:(SEL)action {
    NSButton *button = [NSButton buttonWithTitle:title target:self action:action];
    button.translatesAutoresizingMaskIntoConstraints = NO;
    return button;
}

- (instancetype)init {
    if (!(self = [super init])) return nil;
    _motion = [self check:@"Motion (off draws static stars)"];
    _phaseShift = [self check:@"Phase shift (distance-based red/blue tint)"];
    _speedLabel = [self label:@""];
    _speed = [self sliderMin:SF_SPEED_MIN max:SF_SPEED_MAX step:0.025];
    _densityLabel = [self label:@""];
    _density = [self sliderMin:SF_DENSITY_MIN max:SF_DENSITY_MAX step:0.25];
    _variationLabel = [self label:@""];
    _variation = [self sliderMin:0 max:1 step:0.05];
    _tailLabel = [self label:@""];
    _tail = [self sliderMin:0 max:3 step:0.25];

    NSTextField *note = [self label:
        @"Variation sets each star’s base colour; phase shift adds its distance-based tint.\n"
        @"Higher density may affect performance."];
    note.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
    note.textColor = NSColor.secondaryLabelColor;

    NSButton *defaults = [self button:@"Reset to Defaults" action:@selector(resetDefaults:)];
    NSButton *cancel = [self button:@"Cancel" action:@selector(cancel:)];
    cancel.keyEquivalent = @"\033";
    NSButton *ok = [self button:@"OK" action:@selector(ok:)];
    ok.keyEquivalent = @"\r";

    NSStackView *buttons = [NSStackView stackViewWithViews:@[ defaults, [NSView new], cancel, ok ]];
    buttons.distribution = NSStackViewDistributionFill;
    [buttons.views[1] setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];

    NSStackView *stack = [NSStackView stackViewWithViews:@[
        _motion, _phaseShift, _speedLabel, _speed, _densityLabel, _density,
        _variationLabel, _variation, _tailLabel, _tail, note, buttons
    ]];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeLeading;
    stack.spacing = 8;
    stack.edgeInsets = NSEdgeInsetsMake(20, 20, 20, 20);
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    [stack setCustomSpacing:16 afterView:_phaseShift];
    [stack setCustomSpacing:16 afterView:note];
    [buttons.widthAnchor constraintEqualToAnchor:_speed.widthAnchor].active = YES;

    _window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 360, 420)
                                          styleMask:NSWindowStyleMaskTitled
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    _window.title = @"StarField";
    [_window.contentView addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [stack.leadingAnchor constraintEqualToAnchor:_window.contentView.leadingAnchor],
        [stack.trailingAnchor constraintEqualToAnchor:_window.contentView.trailingAnchor],
        [stack.topAnchor constraintEqualToAnchor:_window.contentView.topAnchor],
        [stack.bottomAnchor constraintEqualToAnchor:_window.contentView.bottomAnchor],
    ]];
    [self applySettings:LoadSettings()];
    return self;
}

- (void)applySettings:(sf_settings)s {
    _motion.state = s.motion ? NSControlStateValueOn : NSControlStateValueOff;
    _phaseShift.state = s.phase_shift ? NSControlStateValueOn : NSControlStateValueOff;
    _speed.doubleValue = s.speed;
    _density.doubleValue = s.density;
    _variation.doubleValue = s.variation;
    _tail.doubleValue = s.tail_length;
    [self changed:nil];
}

- (void)changed:(id)sender {
    for (NSSlider *slider in @[ _speed, _density, _variation, _tail ]) {
        double step = slider.tag / 1000.0;
        double snapped = slider.minValue + round((slider.doubleValue - slider.minValue) / step) * step;
        if (sender == slider || sender == nil) slider.doubleValue = snapped;
    }
    _speedLabel.stringValue =
        [NSString stringWithFormat:@"Speed: %g×", round(_speed.doubleValue * 1000) / 1000];
    _densityLabel.stringValue =
        [NSString stringWithFormat:@"Density: %ld%%", lround(_density.doubleValue * 100)];
    _variationLabel.stringValue =
        [NSString stringWithFormat:@"Star variation: %ld%%", lround(_variation.doubleValue * 100)];
    _tailLabel.stringValue = [NSString stringWithFormat:@"Tail length: %.2f×", _tail.doubleValue];
}

- (void)resetDefaults:(id)sender {
    (void)sender;
    sf_settings d;
    sf_settings_defaults(&d);
    [self applySettings:d];
}

- (void)close {
    NSWindow *parent = _window.sheetParent;
    if (parent) [parent endSheet:_window];
    else [NSApp endSheet:_window];
    [_window orderOut:nil];
}

- (void)cancel:(id)sender {
    (void)sender;
    [self applySettings:LoadSettings()];
    [self close];
}

- (void)ok:(id)sender {
    (void)sender;
    ScreenSaverDefaults *d = StarFieldDefaults();
    [d setDouble:_speed.doubleValue forKey:kSpeed];
    [d setDouble:_density.doubleValue forKey:kDensity];
    [d setDouble:_variation.doubleValue forKey:kVariation];
    [d setDouble:_tail.doubleValue forKey:kTailLength];
    [d setBool:_phaseShift.state == NSControlStateValueOn forKey:kPhaseShift];
    [d setBool:_motion.state == NSControlStateValueOn forKey:kMotion];
    [d synchronize];
    [[NSNotificationCenter defaultCenter] postNotificationName:@"StarFieldSettingsChanged" object:nil];
    [self close];
}

@end

#pragma mark - View

@implementation StarFieldScreenSaverView {
    sf_field _field;
    BOOL _fieldReady;
    sf_settings _settings;
    CGContextRef _bitmap;
    CGImageRef _frame;
    CGSize _logical;
    double _lastTime;
    BOOL _needsStaticFrame;
    StarFieldConfigController *_config;
}

- (instancetype)initWithFrame:(NSRect)frame isPreview:(BOOL)isPreview {
    if (!(self = [super initWithFrame:frame isPreview:isPreview])) return nil;
    self.animationTimeInterval = 1.0 / 60.0;
    self.wantsLayer = YES;
    CGColorRef background = CGColorCreateGenericRGB(
        SF_BACKGROUND_R / 255.0, SF_BACKGROUND_G / 255.0, SF_BACKGROUND_B / 255.0, 1);
    self.layer.backgroundColor = background;
    CGColorRelease(background);
    self.layer.contentsGravity = kCAGravityResize;
    self.layer.magnificationFilter = kCAFilterLinear;
    _fieldReady = sf_field_init(&_field, (uint64_t)mach_absolute_time() ^ (uint64_t)(uintptr_t)self);
    _settings = LoadSettings();
    _lastTime = -1;

    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(settingsChanged:)
                                                 name:@"StarFieldSettingsChanged" object:nil];
    if (!isPreview) {
        /*
         * macOS 14+ hosts savers in legacyScreenSaver, which keeps instances
         * alive (and animating) after the screensaver is dismissed. Stop on the
         * system's will-stop notification so we never burn CPU in the background.
         */
        [[NSDistributedNotificationCenter defaultCenter]
            addObserver:self selector:@selector(willStop:)
                   name:@"com.apple.screensaver.willstop" object:nil];
    }
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [[NSDistributedNotificationCenter defaultCenter] removeObserver:self];
    if (_frame) CGImageRelease(_frame);
    if (_bitmap) CGContextRelease(_bitmap);
    if (_fieldReady) sf_field_free(&_field);
}

- (void)willStop:(NSNotification *)note {
    (void)note;
    [self stopAnimation];
    if ([[[NSProcessInfo processInfo] processName] containsString:@"legacyScreenSaver"]) {
        /* The host is relaunched on next activation; exiting avoids leaked views. */
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.5 * NSEC_PER_SEC)),
                       dispatch_get_main_queue(), ^{ exit(0); });
    }
}

- (void)settingsChanged:(NSNotification *)note {
    (void)note;
    _settings = LoadSettings();
    _needsStaticFrame = YES;
    _lastTime = -1;
    if (!self.isAnimating) [self renderWithSeconds:0];
}

- (double)backingScale {
    NSWindow *window = self.window;
    if (window) return window.backingScaleFactor;
    NSScreen *screen = NSScreen.mainScreen;
    return screen ? screen.backingScaleFactor : 2.0;
}

- (void)rebuild {
    if (!_fieldReady) return;
    CGSize size = self.bounds.size;
    if (size.width < 1 || size.height < 1) return;
    _logical = size;
    double dpr = [self backingScale];
    sf_field_resize(&_field, size.width, size.height, dpr);
    sf_size budget = sf_starfield_size(size.width, size.height, dpr);

    if (_bitmap) CGContextRelease(_bitmap);
    size_t pw = (size_t)MAX(1, floor(size.width * budget.scale));
    size_t ph = (size_t)MAX(1, floor(size.height * budget.scale));
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    _bitmap = CGBitmapContextCreate(NULL, pw, ph, 8, 0, space,
                                    kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Host);
    CGColorSpaceRelease(space);
    if (_bitmap) {
        /* Top-left origin in logical pixels, like the canvas. */
        CGContextTranslateCTM(_bitmap, 0, ph);
        CGContextScaleCTM(_bitmap, (CGFloat)pw / size.width, -(CGFloat)ph / size.height);
        CGContextSetShouldAntialias(_bitmap, true);
        CGContextSetInterpolationQuality(_bitmap, kCGInterpolationHigh);
    }
    _lastTime = -1;
    _needsStaticFrame = YES;
}

- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    [self rebuild];
    [self renderWithSeconds:0];
}

- (void)viewDidChangeBackingProperties {
    [super viewDidChangeBackingProperties];
    [self rebuild];
    [self renderWithSeconds:0];
}

- (void)renderWithSeconds:(double)seconds {
    if (!_fieldReady) return;
    if (!_bitmap || !CGSizeEqualToSize(_logical, self.bounds.size)) [self rebuild];
    if (!_bitmap) return;
    CGContextRef ctx = _bitmap;
    size_t n = sf_field_step(&_field, &_settings, seconds);

    CGContextSetAlpha(ctx, 1);
    CGContextSetRGBFillColor(ctx, SF_BACKGROUND_R / 255.0, SF_BACKGROUND_G / 255.0,
                             SF_BACKGROUND_B / 255.0, 1);
    CGContextFillRect(ctx, CGRectMake(0, 0, _logical.width, _logical.height));
    for (size_t i = 0; i < n; i++) {
        const sf_draw_star *d = &_field.draw[i];
        CGFloat r = d->color.r / 255.0, g = d->color.g / 255.0, b = d->color.b / 255.0;
        if (d->has_tail) {
            /* One filled triangle from a sharp tip to the head's diameter. */
            sf_tail_frame t;
            if (sf_tail_triangle(d, &t)) {
                double bx = t.tip_x + t.along_x, by = t.tip_y + t.along_y;
                CGContextSetRGBFillColor(ctx, r, g, b, d->tail_opacity);
                CGContextBeginPath(ctx);
                CGContextMoveToPoint(ctx, t.tip_x, t.tip_y);
                CGContextAddLineToPoint(ctx, bx + t.across_x, by + t.across_y);
                CGContextAddLineToPoint(ctx, bx - t.across_x, by - t.across_y);
                CGContextClosePath(ctx);
                CGContextFillPath(ctx);
            }
        }
        CGContextSetRGBFillColor(ctx, r, g, b, d->head.opacity);
        double rad = d->head.radius;
        CGContextFillEllipseInRect(ctx, CGRectMake(d->head.x - rad, d->head.y - rad, rad * 2, rad * 2));
    }

    if (_frame) CGImageRelease(_frame);
    _frame = CGBitmapContextCreateImage(ctx);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    self.layer.contents = (__bridge id)_frame;
    [CATransaction commit];
    _needsStaticFrame = NO;
}

- (CGImageRef)currentFrame {
    return _frame;
}

- (void)startAnimation {
    [super startAnimation];
    _settings = LoadSettings();
    _lastTime = -1;
    [self renderWithSeconds:0];
}

- (void)stopAnimation {
    [super stopAnimation];
    _lastTime = -1;
}

- (void)drawRect:(NSRect)rect {
    /* The layer shows the rendered frame; this only covers the first instant. */
    [[NSColor colorWithSRGBRed:SF_BACKGROUND_R / 255.0 green:SF_BACKGROUND_G / 255.0
                          blue:SF_BACKGROUND_B / 255.0 alpha:1] setFill];
    NSRectFill(rect);
}

- (void)animateOneFrame {
    if (!_settings.motion) {
        if (_needsStaticFrame) [self renderWithSeconds:0];
        return;
    }
    double now = CACurrentMediaTime();
    /* No catch-up after a stall: the engine clamps each step to 50 ms. */
    double seconds = _lastTime < 0 ? 0 : now - _lastTime;
    _lastTime = now;
    [self renderWithSeconds:seconds];
}

- (BOOL)hasConfigureSheet {
    return YES;
}

- (NSWindow *)configureSheet {
    if (!_config) _config = [StarFieldConfigController new];
    else [_config applySettings:LoadSettings()];
    return _config.window;
}

@end
