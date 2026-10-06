#!/usr/bin/env python3
"""Exercise the actual publication functions and preference migration without a device."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_installation_routing import ROOT, method


def run(source, extension, flags):
    with tempfile.TemporaryDirectory(prefix='theft4-optional-performance-') as directory:
        path = Path(directory)
        file = path / ('test.' + extension)
        file.write_text(source)
        subprocess.run(['xcrun', 'clang++' if extension == 'cpp' else 'clang',
                        '-Werror', '-I', str(ROOT / 'ios/bridge'), *flags,
                        str(file), '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True, timeout=10)


class OptionalPerformanceControls(unittest.TestCase):
    def test_actual_publication_functions_in_optimized_mode(self):
        source = (ROOT / 'ios/bridge/theft4_metal_presenter.mm').read_text()
        signatures = ['void theft4_frame_counter_note_published(void)',
                      'void theft4_frame_counter_set_enabled(bool enabled)',
                      'uint64_t theft4_publication_capture_start(void)',
                      'void theft4_publication_capture_stop(void)',
                      'void theft4_frame_time_set_enabled(bool enabled)']
        # Inline one-line functions use their real bodies, too.
        def function(signature):
            a = source.index(signature + ' {')
            b = source.index('}', a) + 1 if signature in signatures[2:4] else source.index('\n}', a) + 2
            return source[a:b]
        tick_source = (ROOT / 'glue/rexglue-sdk-main/src/graphics/gta4_native/metal_frame_frontend.inc').read_text()
        a = tick_source.index('  const auto tick=[]')
        tick = tick_source[a:tick_source.index('\n', a)]
        run(r'''
#include "theft4_frame_time_history.h"
#include "theft4_publication_trace.h"
#include "theft4_retail_mode.h"
#include <cassert>
#include <cmath>
static double now = 1;
static double CACurrentMediaTime() { return now; }
static std::atomic<uint64_t> published_game_frames{0};
static std::atomic<bool> fps_counter_enabled{false};
static theft4::FrameTimeHistory<> frame_time_history;
static theft4::PublicationTrace<> publication_trace;
namespace light { static void* current = nullptr; }
namespace rex::chrono { struct Clock { static uint64_t QueryHostTickCount() { return 42; } }; }
''' + '\n'.join(function(s) for s in signatures) + r'''
int main() {
    setenv("THEFT4_RETAIL_MODE", "1", 1);
    theft4_apply_retail_diagnostic_policy();
    assert(theft4_retail_mode());
    // Normal play does not count frames, read clocks, or fill either ring.
    theft4_frame_counter_note_published();
    assert(published_game_frames.load() == 0 && !publication_trace.Enabled());
    // FPS alone counts publications, with neither graph nor capture armed.
    theft4_frame_counter_set_enabled(true);
    theft4_frame_counter_note_published();
    assert(published_game_frames.load() == 1 && !frame_time_history.Enabled());
    theft4_frame_counter_set_enabled(false);
    theft4_frame_counter_note_published(); assert(published_game_frames.load() == 1);
    // Graph alone collects actual publication intervals in optimized mode.
    theft4_frame_time_set_enabled(true);
    theft4_frame_counter_note_published(); now += 0.033;
    theft4_frame_counter_note_published();
    double values[8], pending;
    assert(frame_time_history.Copy(uint64_t(now*1e9), values, 8, pending) == 1);
    assert(std::abs(values[0]-33.0) < .00001);
    theft4_frame_time_set_enabled(false);
    // Long capture works with all overlays hidden and developer policy intact.
    auto cursor = theft4_publication_capture_start();
    now += .04; theft4_frame_counter_note_published();
    theft4::PublicationSample rows[8]; uint64_t lost = 0;
    assert(publication_trace.CopyAfter(cursor, rows, 8, lost) == 1 && lost == 0);
    assert(rows[0].frame == 4 && rows[0].monotonic_ns > 0);
    assert(!strcmp(getenv("THEFT4_METAL_PASS_PROFILING"), "0"));
    assert(!strcmp(getenv("THEFT4_DIAGNOSTICS"), "0"));
    theft4_publication_capture_stop();
    theft4_frame_counter_note_published(); assert(published_game_frames.load() == 4);
    // Actual frontend stage clock: off normally, nonzero only for capture.
''' + tick + r'''
    assert(tick() == 0);
    int active = 1; light::current = &active; assert(tick() == 42);
    light::current = nullptr; assert(tick() == 0);
    puts("PASS: optimized normal play, independent FPS/graph/capture and capture-only frontend clocks");
}
''', 'cpp', ['-std=c++20'])

    def test_actual_overlay_preference_migration(self):
        source = (ROOT / 'ios/Theft4/main.m').read_text()
        run(r'''
#import <Foundation/Foundation.h>
#include <assert.h>
static NSMutableDictionary *preferences;
@interface DefaultsDouble : NSObject
+ (instancetype)standardUserDefaults;
- (BOOL)boolForKey:(NSString *)key;
- (void)setBool:(BOOL)value forKey:(NSString *)key;
- (void)removeObjectForKey:(NSString *)key;
@end
@implementation DefaultsDouble
+ (instancetype)standardUserDefaults { static id d; if (!d) d = [self new]; return d; }
- (BOOL)boolForKey:(NSString *)key { return [preferences[key] boolValue]; }
- (void)setBool:(BOOL)value forKey:(NSString *)key { preferences[key] = @(value); }
- (void)removeObjectForKey:(NSString *)key { [preferences removeObjectForKey:key]; }
@end
#define NSUserDefaults DefaultsDouble
@interface Controller : NSObject
- (void)migratePerformanceOverlayDefaults;
@end
@implementation Controller
''' + method(source, '- (void)migratePerformanceOverlayDefaults') + r'''
@end
int main(void) { @autoreleasepool {
    Controller *c = [Controller new];
    // An old retail install hid these preferences: do not activate them on upgrade.
    preferences = [@{@"Theft4RetailMode":@YES, @"Theft4ShowFPS":@YES,
      @"Theft4ShowCPUUsage":@YES, @"Theft4ShowFrameTime":@YES, @"GameSetting":@7} mutableCopy];
    [c migratePerformanceOverlayDefaults];
    for (NSString *key in @[@"Theft4ShowFPS", @"Theft4ShowCPUUsage", @"Theft4ShowFrameTime"])
      assert(![preferences[key] boolValue]);
    assert(preferences[@"Theft4RetailMode"] == nil && [preferences[@"GameSetting"] intValue] == 7);
    // User choices on 0.3 persist across launches; migration only runs once.
    preferences[@"Theft4ShowFrameTime"] = @YES;
    [c migratePerformanceOverlayDefaults]; assert([preferences[@"Theft4ShowFrameTime"] boolValue]);
    // Active overlays on old diagnostic installs were deliberate: retain them.
    preferences = [@{@"Theft4RetailMode":@NO, @"Theft4ShowFPS":@YES} mutableCopy];
    [c migratePerformanceOverlayDefaults]; assert([preferences[@"Theft4ShowFPS"] boolValue]);
    preferences = [NSMutableDictionary new]; [c migratePerformanceOverlayDefaults];
    assert(![preferences[@"Theft4ShowFPS"] boolValue]);
    puts("PASS: hidden preferences migrated once; explicit overlays retained; fresh defaults off");
} return 0; }
''', 'm', ['-fobjc-arc', '-framework', 'Foundation'])

    def test_launcher_and_timer_controls_are_independent(self):
        source = (ROOT / 'ios/Theft4/main.m').read_text()
        launcher = (ROOT / 'ios/Theft4/Theft4LauncherView.m').read_text()
        self.assertNotIn('retailMode', source + launcher)
        for name in ['- (void)updateFrameTimeHUD', '- (BOOL)beginPublicationCapture',
                     '- (void)displaySettingsChanged:(UIControl *)sender']:
            self.assertNotIn('if (theft4_retail_mode())', method(source, name))
        self.assertIn('sampleFPS || _publicationCaptureActive', method(source, '- (void)updateFrameTimeHUD'))
        self.assertIn('[_fpsTimer invalidate]', method(source, '- (void)updateFrameTimeHUD'))
        self.assertIn('[self updateFrameTimeHUD]', method(source, '- (void)stopPublicationCapture'))
        self.assertIn('@"Theft4ShowFPS": @NO', source)
        self.assertIn('@"Theft4ShowCPUUsage": @NO', source)
        self.assertNotIn('boolForKey:@"Theft4RetailMode"', source[source.index('int main(int argc'):])

if __name__ == '__main__': unittest.main()
