// Layout and analog math adapted from XeniOS touch_layout_ios_defaults.cc and
// touch_input_resolver.cc, copyright 2026 Ben Vanik. BSD-3-Clause; see bundled
// XeniOS-Touch-LICENSE.txt. UIKit capture/bridge integration is Theft4-specific.
#import "Theft4TouchControls.h"
#import <QuartzCore/QuartzCore.h>
#include <math.h>
#include "theft4_touch_input.h"

typedef struct { const char *label; CGFloat x, y, w, h; uint16_t button; int trigger; } PadControl;
// XeniOS FPS Compact normalized positions, with explicit D-pad / stick-click
// buttons instead of its editor's combined ring and secondary tap gestures.
static const PadControl controls[] = {
    {"Move", .055, .56, .190, .315, 0, 0},
    {"Back", .390, .045, .080, .112, 0x20, 0},
    {"Start", .495, .045, .085, .112, 0x10, 0},
    {"LB", .660, .050, .085, .112, 0x100, 0},
    {"RB", .765, .050, .085, .112, 0x200, 0},
    {"LT", .095, .405, .120, .110, 0, 1},
    {"Y", .760, .455, .065, .115, 0x8000, 0},
    {"X", .700, .585, .065, .115, 0x4000, 0},
    {"B", .820, .585, .065, .115, 0x2000, 0},
    {"A", .760, .715, .065, .115, 0x1000, 0},
    {"RT", .860, .405, .120, .110, 0, 2},
    {"↑", .315, .68, .055, .085, 0x1, 0},
    {"↓", .315, .86, .055, .085, 0x2, 0},
    {"←", .258, .77, .055, .085, 0x4, 0},
    {"→", .372, .77, .055, .085, 0x8, 0},
    {"L3", .440, .85, .065, .10, 0x40, 0},
    {"R3", .530, .85, .065, .10, 0x80, 0},
};
static const NSInteger controlCount = sizeof(controls) / sizeof(controls[0]);

@interface Theft4TouchControls () {
    NSMutableArray<UILabel *> *_labels;
    NSMapTable<UITouch *, NSNumber *> *_captures;
    UIView *_stickKnob;
    NSTimer *_lookTimer;
    CGPoint _lookDelta;
    int16_t _lookX, _lookY;
    CFTimeInterval _lastTick;
    CGRect _lastBounds;
    UIEdgeInsets _lastInsets;
}
@end

@implementation Theft4TouchControls
- (instancetype)initWithFrame:(CGRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.multipleTouchEnabled = YES;
        self.hidden = YES;
        self.accessibilityIdentifier = @"game.touchControls";
        _captures = [NSMapTable strongToStrongObjectsMapTable];
        _labels = [NSMutableArray new];
        for (NSInteger i = 0; i < controlCount; ++i) {
            UILabel *label = [UILabel new];
            label.text = [NSString stringWithUTF8String:controls[i].label];
            label.textAlignment = NSTextAlignmentCenter;
            label.font = [UIFont systemFontOfSize:17 weight:UIFontWeightSemibold];
            label.adjustsFontSizeToFitWidth = YES;
            label.textColor = UIColor.whiteColor;
            label.backgroundColor = [UIColor colorWithWhite:0 alpha:.25];
            label.layer.borderColor = [UIColor colorWithWhite:1 alpha:.55].CGColor;
            label.layer.borderWidth = 1.5;
            label.layer.masksToBounds = YES;
            label.userInteractionEnabled = NO;
            [_labels addObject:label];
            [self addSubview:label];
        }
        _stickKnob = [UIView new];
        _stickKnob.userInteractionEnabled = NO;
        _stickKnob.backgroundColor = [UIColor colorWithWhite:1 alpha:.25];
        [self addSubview:_stickKnob];
    }
    return self;
}
- (void)layoutSubviews {
    [super layoutSubviews];
    if (!CGRectEqualToRect(_lastBounds, self.bounds) ||
        !UIEdgeInsetsEqualToEdgeInsets(_lastInsets, self.safeAreaInsets)) {
        [self releaseAllTouches];
        _lastBounds = self.bounds;
        _lastInsets = self.safeAreaInsets;
    }
    CGRect area = UIEdgeInsetsInsetRect(self.bounds, self.safeAreaInsets);
    for (NSInteger i = 0; i < controlCount; ++i) {
        PadControl c = controls[i];
        CGFloat w = MAX(36, c.w * area.size.width);
        CGFloat h = MAX(36, c.h * area.size.height);
        // Circular movement pad; landscape positions remain proportional.
        if (i == 0) w = h = MIN(w, h);
        _labels[i].frame = CGRectMake(area.origin.x + c.x * area.size.width,
            area.origin.y + c.y * area.size.height, w, h);
        _labels[i].layer.cornerRadius = i == 0 ? w / 2 : MIN(w, h) / 3;
    }
    CGFloat diameter = _labels[0].bounds.size.width * .36;
    _stickKnob.bounds = CGRectMake(0, 0, diameter, diameter);
    _stickKnob.layer.cornerRadius = diameter / 2;
    _stickKnob.center = _labels[0].center;
}
- (void)setActive:(BOOL)active {
    if (_active == active) return;
    _active = active;
    self.hidden = !active;
    [self releaseAllTouches];
    [_lookTimer invalidate];
    _lookTimer = nil;
    if (active) {
        _lastTick = CACurrentMediaTime();
        __weak Theft4TouchControls *weakSelf = self;
        _lookTimer = [NSTimer timerWithTimeInterval:1.0 / 60.0 repeats:YES
            block:^(NSTimer *timer) { [weakSelf publishTouchState]; }];
        [NSRunLoop.mainRunLoop addTimer:_lookTimer forMode:NSRunLoopCommonModes];
    }
}
- (void)releaseAllTouches {
    [_captures removeAllObjects];
    _lookDelta = CGPointZero;
    _lookX = _lookY = 0;
    theft4_touch_reset();
    _stickKnob.center = _labels[0].center;
    for (UILabel *label in _labels)
        label.backgroundColor = [UIColor colorWithWhite:0 alpha:.25];
}
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if (!_active) return;
    for (UITouch *touch in touches) {
        CGPoint p = [touch locationInView:self];
        NSInteger capture = -1; // Unoccupied background is swipe-to-look.
        for (NSInteger i = controlCount - 1; i >= 0; --i) {
            if (CGRectContainsPoint(_labels[i].frame, p)) { capture = i; break; }
        }
        // Only one movement and one look capture; buttons are independently held.
        if ((capture == 0 || capture == -1) &&
            [[_captures.objectEnumerator allObjects] containsObject:@(capture)]) continue;
        [_captures setObject:@(capture) forKey:touch];
    }
    [self publishButtonsAndMovement];
}
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if (!_active) return;
    for (UITouch *touch in touches) {
        NSNumber *capture = [_captures objectForKey:touch];
        if (capture && capture.integerValue == -1) {
            CGPoint p = [touch locationInView:self], previous = [touch previousLocationInView:self];
            _lookDelta.x += p.x - previous.x;
            _lookDelta.y += p.y - previous.y;
        }
    }
    [self publishButtonsAndMovement];
}
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    for (UITouch *touch in touches) {
        if ([[_captures objectForKey:touch] isEqual:@(-1)]) {
            _lookDelta = CGPointZero;
            _lookX = _lookY = 0;
        }
        [_captures removeObjectForKey:touch];
    }
    [self publishButtonsAndMovement];
}
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [self touchesEnded:touches withEvent:event];
}
- (theft4_touch_pad)buttonsAndMovement {
    theft4_touch_pad state = {0};
    for (UILabel *label in _labels)
        label.backgroundColor = [UIColor colorWithWhite:0 alpha:.25];
    _stickKnob.center = _labels[0].center;
    for (UITouch *touch in _captures) {
        NSInteger i = [[_captures objectForKey:touch] integerValue];
        if (i < 0) continue;
        CGPoint p = [touch locationInView:self];
        if (i == 0) {
            CGPoint center = _labels[0].center;
            CGFloat radius = MAX(1, _labels[0].bounds.size.width * .48);
            CGFloat x = (p.x - center.x) / radius, y = (center.y - p.y) / radius;
            CGFloat magnitude = hypot(x, y);
            CGFloat strength = MIN(1, MAX(0, (magnitude - .14) / .86));
            if (magnitude > 0) { x = x / magnitude * strength; y = y / magnitude * strength; }
            state.lx = (int16_t)lround(x * 32767);
            state.ly = (int16_t)lround(y * 32767);
            _stickKnob.center = CGPointMake(center.x + x * radius * .65, center.y - y * radius * .65);
        } else if (CGRectContainsPoint(CGRectInset(_labels[i].frame, -10, -10), p)) {
            state.buttons |= controls[i].button;
            if (controls[i].trigger == 1) state.left_trigger = 255;
            if (controls[i].trigger == 2) state.right_trigger = 255;
            _labels[i].backgroundColor = [UIColor colorWithWhite:1 alpha:.35];
        }
    }
    return state;
}
- (void)publishButtonsAndMovement {
    // Immediate releases/buttons; camera deltas are consumed only by the timer.
    theft4_touch_pad state = [self buttonsAndMovement];
    state.rx = _lookX;
    state.ry = _lookY;
    theft4_touch_store(state);
}
- (void)publishTouchState {
    if (!_active) return;
    theft4_touch_pad state = [self buttonsAndMovement];
    CFTimeInterval now = CACurrentMediaTime();
    double elapsed = MAX(1.0 / 240.0, now - _lastTick);
    _lastTick = now;
    // Normalize swipe distance to a 60 Hz sample, independent of touch delivery.
    double scale = 1.0 / (18.0 * elapsed * 60.0);
    state.rx = (int16_t)lround(MAX(-1, MIN(1, _lookDelta.x * scale)) * 32767);
    state.ry = (int16_t)lround(MAX(-1, MIN(1, -_lookDelta.y * scale)) * 32767);
    _lookX = state.rx;
    _lookY = state.ry;
    _lookDelta = CGPointZero;
    theft4_touch_store(state);
}
- (void)dealloc { [_lookTimer invalidate]; }
@end
