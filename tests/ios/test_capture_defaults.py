#!/usr/bin/env python3
"""Automatic capture must be opt-in per launcher session; short profiles stay independent."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_installation_routing import ROOT, method


class CaptureDefaultsTests(unittest.TestCase):
    def test_actual_session_reset_retires_old_enabled_preference(self):
        source = (ROOT / "ios/Theft4/main.m").read_text()
        reset = method(source, "- (void)resetAutomaticCaptureForNewSession")
        harness = r'''
#import <Foundation/Foundation.h>
#include <assert.h>
static NSMutableDictionary *saved;
@interface DefaultsDouble : NSObject
+ (instancetype)standardUserDefaults;
- (void)removeObjectForKey:(NSString *)key;
@end
@implementation DefaultsDouble
+ (instancetype)standardUserDefaults { static id d; if (!d) d = [self new]; return d; }
- (void)removeObjectForKey:(NSString *)key { [saved removeObjectForKey:key]; }
@end
#define NSUserDefaults DefaultsDouble
@interface SwitchDouble : NSObject
@property BOOL on;
@end
@implementation SwitchDouble
@end
@interface Controller : NSObject {
@public
    SwitchDouble *_performanceCapture;
}
- (void)resetAutomaticCaptureForNewSession;
@end
@implementation Controller
''' + reset + r'''
@end
int main(void) { @autoreleasepool {
    for (id previous in @[[NSNull null], @NO, @YES]) {
        saved = [@{@"Theft4ShowFrameTime": @YES, @"Theft4Round1ConstantReuse": @YES} mutableCopy];
        if (previous != NSNull.null) saved[@"Theft4DetailedPerformanceCapture"] = previous;
        Controller *c = [Controller new]; c->_performanceCapture = [SwitchDouble new];
        c->_performanceCapture.on = YES;
        [c resetAutomaticCaptureForNewSession];
        assert(!c->_performanceCapture.on);
        assert(saved[@"Theft4DetailedPerformanceCapture"] == nil);
        assert([saved[@"Theft4ShowFrameTime"] boolValue]);
        assert([saved[@"Theft4Round1ConstantReuse"] boolValue]);
        // An explicit choice still arms this session; another launch resets it.
        c->_performanceCapture.on = YES; assert(c->_performanceCapture.on);
        [c resetAutomaticCaptureForNewSession]; assert(!c->_performanceCapture.on);
    }
    puts("PASS: fresh/off/on legacy preferences reset; unrelated settings preserved; explicit session opt-in works");
} return 0; }
'''
        with tempfile.TemporaryDirectory(prefix="theft4-capture-defaults-") as directory:
            path = Path(directory)
            (path / "defaults.m").write_text(harness)
            subprocess.run(["xcrun", "clang", "-fobjc-arc", "-Werror", "-framework", "Foundation",
                            str(path / "defaults.m"), "-o", str(path / "defaults")], check=True)
            subprocess.run([str(path / "defaults")], check=True, timeout=10)

    def test_new_session_hook_and_short_capture_independence(self):
        source = (ROOT / "ios/Theft4/main.m").read_text()
        self.assertIn("[self resetAutomaticCaptureForNewSession]", method(source, "- (void)viewDidLoad"))
        self.assertEqual(source.count('@"Theft4DetailedPerformanceCapture"'), 1)
        self.assertNotIn("_performanceCapture", method(source, "- (void)displaySettingsChanged:(UIControl *)sender"))
        short = method(source, "- (void)requestNativeProfile")
        self.assertNotIn("_performanceCapture", short)
        self.assertIn("rex_gta4_native_profile_start()", short)
        self.assertIn("if (_performanceCapture.on) [self beginPublicationCapture]", source)


if __name__ == "__main__":
    unittest.main()
