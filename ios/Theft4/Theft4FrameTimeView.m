#import "Theft4FrameTimeView.h"
#include <math.h>

@implementation Theft4FrameTimeView {
    theft4_frame_time_snapshot _snapshot;
    BOOL _captureRequested;
    BOOL _captureCompleted;
    BOOL _captureFailed;
}
- (instancetype)initWithFrame:(CGRect)frame {
    if (!(self = [super initWithFrame:frame])) return nil;
    self.backgroundColor = [UIColor colorWithWhite:0 alpha:0.68];
    self.layer.cornerRadius = 7;
    self.clipsToBounds = YES;
    self.userInteractionEnabled = YES;
    self.opaque = NO;
    self.isAccessibilityElement = YES;
    self.accessibilityIdentifier = @"game.frameTime";
    self.accessibilityLabel = @"Frame-time graph";
    self.accessibilityHint = @"Double-tap to capture 600 profiled frames";
    return self;
}
- (void)setCaptureRequested:(BOOL)requested {
    _captureRequested = requested;
    if (requested) {
        _captureCompleted = NO;
        _captureFailed = NO;
    }
    self.layer.borderWidth = requested ? 2.0 : 0.0;
    self.layer.borderColor = requested
        ? [UIColor colorWithRed:1 green:0.56 blue:0.16 alpha:1].CGColor
        : UIColor.clearColor.CGColor;
    self.accessibilityHint = requested
        ? @"Frame-timing capture requested for this launch"
        : @"Double-tap to capture 600 profiled frames";
    [self setNeedsDisplay];
}
- (void)setCaptureCompleted:(BOOL)success {
    _captureRequested = NO;
    _captureCompleted = success;
    _captureFailed = !success;
    self.layer.borderWidth = 2.0;
    self.layer.borderColor = (success
        ? [UIColor colorWithRed:0.25 green:0.82 blue:0.48 alpha:1]
        : [UIColor colorWithRed:1 green:0.32 blue:0.32 alpha:1]).CGColor;
    self.accessibilityHint = success
        ? @"Frame-timing capture saved"
        : @"Frame-timing capture export failed";
    [self setNeedsDisplay];
}
- (void)updateWithSnapshot:(const theft4_frame_time_snapshot *)snapshot {
    _snapshot = *snapshot;
    _snapshot.count = MIN(_snapshot.count, THEFT4_FRAME_TIME_SAMPLES);
    [self setNeedsDisplay];
}
- (void)drawRect:(CGRect)rect {
    const double target = 1000.0 / 30.0;
    double last = _snapshot.count ? _snapshot.milliseconds[_snapshot.count - 1] : 0;
    // A still-unfinished frame must remain visible during a stall. Only extend
    // past the last completed interval, so polling never creates short frames.
    const BOOL pending = _snapshot.pending_ms > MAX(target, last);
    if (pending) last = _snapshot.pending_ms;
    double peak = last;
    for (uint32_t i = 0; i < _snapshot.count; ++i) peak = MAX(peak, _snapshot.milliseconds[i]);
    NSString *title = (last > 0) ?
        [NSString stringWithFormat:@"%5.1f ms%@  peak %.1f", last, pending ? @"+" : @" ", peak] :
        @"Frame time · waiting";
    NSDictionary *text = @{NSFontAttributeName:[UIFont monospacedDigitSystemFontOfSize:11 weight:UIFontWeightMedium],
                           NSForegroundColorAttributeName:UIColor.whiteColor};
    [title drawAtPoint:CGPointMake(9, 6) withAttributes:text];
    if (_captureRequested) {
        [@"REC" drawAtPoint:CGPointMake(self.bounds.size.width - 31, 6)
            withAttributes:@{NSFontAttributeName:[UIFont monospacedSystemFontOfSize:10
                                                                            weight:UIFontWeightBold],
                             NSForegroundColorAttributeName:[UIColor colorWithRed:1
                                                                             green:0.65
                                                                              blue:0.25
                                                                             alpha:1]}];
    } else if (_captureCompleted || _captureFailed) {
        NSString *state = _captureCompleted ? @"SAVED" : @"ERR";
        UIColor *color = _captureCompleted
            ? [UIColor colorWithRed:0.38 green:1 blue:0.62 alpha:1]
            : [UIColor colorWithRed:1 green:0.4 blue:0.4 alpha:1];
        [state drawAtPoint:CGPointMake(self.bounds.size.width - (_captureCompleted ? 43 : 31), 6)
            withAttributes:@{NSFontAttributeName:[UIFont monospacedSystemFontOfSize:10
                                                                            weight:UIFontWeightBold],
                             NSForegroundColorAttributeName:color}];
    }
    self.accessibilityValue = title;
    CGRect plot = CGRectMake(9, 28, self.bounds.size.width - 18, self.bounds.size.height - 46);
    double ceiling = MAX(66.667, MIN(200, peak * 1.15));
    CGFloat (^y)(double) = ^CGFloat(double ms) {
        return CGRectGetMaxY(plot) - MIN(1, MAX(0, ms / ceiling)) * plot.size.height;
    };
    CGContextRef context = UIGraphicsGetCurrentContext();
    CGContextSetStrokeColorWithColor(context, [UIColor colorWithWhite:1 alpha:0.38].CGColor);
    CGContextSetLineWidth(context, 0.75);
    CGFloat dash[] = {3, 3}; CGContextSetLineDash(context, 0, dash, 2);
    CGContextMoveToPoint(context, plot.origin.x, y(target));
    CGContextAddLineToPoint(context, CGRectGetMaxX(plot), y(target)); CGContextStrokePath(context);
    CGContextSetLineDash(context, 0, NULL, 0);
    UIColor *teal = [UIColor colorWithRed:0.35 green:0.93 blue:0.77 alpha:1];
    UIColor *amber = [UIColor colorWithRed:1 green:0.76 blue:0.28 alpha:1];
    UIColor *red = [UIColor colorWithRed:1 green:0.35 blue:0.38 alpha:1];
    uint32_t total = _snapshot.count + (pending ? 1 : 0);
    CGFloat step = plot.size.width / THEFT4_FRAME_TIME_SAMPLES;
    for (uint32_t i = 1; i < total; ++i) {
        double a = _snapshot.milliseconds[i - 1];
        double b = i < _snapshot.count ? _snapshot.milliseconds[i] : last;
        double worst = MAX(a, b);
        CGContextSetStrokeColorWithColor(context, (worst > 50 ? red : worst > 35 ? amber : teal).CGColor);
        CGContextSetLineWidth(context, 1.3);
        CGFloat x = CGRectGetMaxX(plot) - (total - i) * step;
        CGContextMoveToPoint(context, x, y(a));
        CGContextAddLineToPoint(context, x + step, y(b)); CGContextStrokePath(context);
    }
    [@"33.3 ms target · 180 frames" drawAtPoint:CGPointMake(9, self.bounds.size.height - 15)
        withAttributes:@{NSFontAttributeName:[UIFont systemFontOfSize:9],
                         NSForegroundColorAttributeName:[UIColor colorWithWhite:1 alpha:0.7]}];
}
@end
