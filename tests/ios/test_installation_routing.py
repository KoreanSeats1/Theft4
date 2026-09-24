#!/usr/bin/env python3
"""Run actual launcher routing methods against Foundation-only UI/validator doubles.

No game assets, devices, or real app preferences are modified. The canonical
installer itself is unchanged; these tests exercise its caller's state machine,
including asynchronous base checks and foreground reconciliation.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "ios/Theft4/main.m"


def method(source, signature):
    start = source.index(signature + " {")
    end = source.index("\n}\n", start) + 3
    return source[start:end]


PREAMBLE = r'''
#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>
#include <assert.h>
#define THEFT4_HAS_GAME_LOADER 1
static NSMutableDictionary *preferences;
@interface TestDefaults : NSObject
+ (instancetype)standardUserDefaults;
- (BOOL)boolForKey:(NSString *)key;
- (void)setBool:(BOOL)value forKey:(NSString *)key;
@end
@implementation TestDefaults
+ (instancetype)standardUserDefaults { static TestDefaults *d; if (!d) d = [self new]; return d; }
- (BOOL)boolForKey:(NSString *)key { return [preferences[key] boolValue]; }
- (void)setBool:(BOOL)value forKey:(NSString *)key { preferences[key] = @(value); }
@end
#define NSUserDefaults TestDefaults
static NSString *Theft4SetupCompleteDefaultsKey(void) { return @"complete"; }
static NSString *Theft4InstallationDirectoryCreatedDefaultsKey(void) { return @"created"; }
static NSString *Theft4FilesGamePath(void) { return @"Theft4/game"; }
static NSString *Theft4DisplayName(void) { return @"Theft4"; }
static int installedChecks, baseChecks;
static BOOL patchArrivesDuringBaseCheck;
static NSString *readFile(NSString *root, NSString *name) {
    return [NSString stringWithContentsOfFile:[root stringByAppendingPathComponent:name]
                                    encoding:NSUTF8StringEncoding error:nil];
}
static void writeFile(NSString *root, NSString *name, NSString *value) {
    assert([value writeToFile:[root stringByAppendingPathComponent:name] atomically:YES
                    encoding:NSUTF8StringEncoding error:nil]);
}
static int theft4_validate_installed_game(const char *path, char *message, size_t capacity) {
    ++installedChecks;
    NSString *root = @(path), *base = readFile(root, @"default.xex");
    BOOL ready = [base isEqual:@"patched"] || ([base isEqual:@"base"] &&
        [readFile(root, @"default.xexp") isEqual:@"matching-update"]);
    snprintf(message, capacity, "%s", ready ? "Ready" : "Missing or invalid update");
    return ready ? 0 : 1;
}
static int theft4_validate_base_game(const char *path, char *message, size_t capacity) {
    ++baseChecks;
    BOOL ready = [readFile(@(path), @"default.xex") isEqual:@"base"];
    if (patchArrivesDuringBaseCheck) writeFile(@(path), @"default.xexp", @"matching-update");
    snprintf(message, capacity, "%s", ready ? "Base verified" : "Unsupported base");
    return ready ? 0 : 1;
}
@interface ViewDouble : NSObject
@property BOOL hidden;
- (void)stopAnimating;
@end
@implementation ViewDouble
- (void)stopAnimating {}
@end
'''

INTERFACE = r'''
@interface Routing : NSObject {
@public
    NSURL *_gameURL;
    BOOL _loading, _executionAttempted, _saveTransferBusy;
    BOOL _baseGameChecked, _baseGameCheckAttempted, _setupValidated, _installationFlowPresented;
    NSString *_failure, *_bootStatus, *_lastTitle;
    Theft4InstallationStep _installationStep;
    ViewDouble *_installationOverlay, *_installationSpinner;
    int _events;
}
@property id presentedViewController;
- (BOOL)hasInstalledBaseAndUpdate;
- (BOOL)isSetupComplete;
- (void)markSetupComplete;
- (BOOL)completeSetupForInstalledGame;
- (void)refreshInstallationFlow;
- (void)presentInstallationFlowIfNeeded;
- (void)routeInstallationFlow;
- (void)checkBaseGame;
- (void)refresh;
- (void)record:(NSString *)event;
- (void)showInstallationTitle:(NSString *)title detail:(NSString *)detail
                  actionTitle:(NSString *)actionTitle spinner:(BOOL)spinner step:(Theft4InstallationStep)step;
@end
@implementation Routing
- (void)refresh {}
- (void)record:(NSString *)event { ++_events; }
- (void)showInstallationTitle:(NSString *)title detail:(NSString *)detail
                  actionTitle:(NSString *)actionTitle spinner:(BOOL)spinner step:(Theft4InstallationStep)step {
    _lastTitle = title; _installationStep = step; _installationOverlay.hidden = NO;
}
'''

TESTS = r'''
@end
static Routing *fixture(NSString *root, NSString *base, NSString *patch) {
    NSString *folder = [root stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    assert([NSFileManager.defaultManager createDirectoryAtPath:folder
              withIntermediateDirectories:YES attributes:nil error:nil]);
    if (base) writeFile(folder, @"default.xex", base);
    if (patch) writeFile(folder, @"default.xexp", patch);
    preferences = [@{@"created": @YES} mutableCopy];
    Routing *r = [Routing new]; r->_gameURL = [NSURL fileURLWithPath:folder];
    r->_installationOverlay = [ViewDouble new]; r->_installationSpinner = [ViewDouble new];
    installedChecks = baseChecks = 0; patchArrivesDuringBaseCheck = NO;
    return r;
}
static void awaitCheck(Routing *r) {
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:5];
    while (r->_installationStep == Theft4InstallationStepCheckingBaseGame && deadline.timeIntervalSinceNow > 0)
        [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.005]];
    assert(r->_installationStep != Theft4InstallationStepCheckingBaseGame);
}
static void assertReady(Routing *r) {
    assert(r->_installationStep == Theft4InstallationStepReady);
    assert(r->_installationOverlay.hidden);
    assert([preferences[@"complete"] boolValue]);
}
int main(int argc, char **argv) { @autoreleasepool {
    assert(argc == 2); NSString *root = @(argv[1]);
    // Existing pair: no preference required; repeat appearance is idempotent.
    Routing *r = fixture(root, @"base", @"matching-update");
    [r refreshInstallationFlow]; assertReady(r); assert(r->_events == 1);
    [r refreshInstallationFlow]; assertReady(r); assert(r->_events == 1);
    // Canonically accepted prepatched executable: no sibling patch demanded.
    r = fixture(root, @"patched", nil); [r refreshInstallationFlow]; assertReady(r);
    assert(baseChecks == 0);
    r = fixture(root, @"patched", nil); [r checkBaseGame]; assertReady(r); assert(baseChecks == 0);
    // Base only still needs an update; copying it while backgrounded resumes setup.
    r = fixture(root, @"base", nil); [r refreshInstallationFlow];
    assert(r->_installationStep == Theft4InstallationStepCopyBaseGame);
    [r checkBaseGame]; [r checkBaseGame]; awaitCheck(r); assert(baseChecks == 1);
    assert(r->_installationStep == Theft4InstallationStepSelectingUpdate);
    writeFile(r->_gameURL.path, @"default.xexp", @"matching-update");
    [r refreshInstallationFlow]; assertReady(r);
    // Full installation copied after the initial empty-folder prompt.
    r = fixture(root, nil, nil); [r refreshInstallationFlow];
    assert(r->_installationStep == Theft4InstallationStepCopyBaseGame);
    writeFile(r->_gameURL.path, @"default.xex", @"base");
    writeFile(r->_gameURL.path, @"default.xexp", @"matching-update");
    [r refreshInstallationFlow]; assertReady(r);
    // Stale saved completion / wrong update / corrupt base cannot bypass validation.
    r = fixture(root, @"base", @"wrong-update"); preferences[@"complete"] = @YES;
    assert(![r isSetupComplete]); [r refreshInstallationFlow]; [r checkBaseGame]; awaitCheck(r);
    assert(r->_installationStep == Theft4InstallationStepSelectingUpdate);
    r = fixture(root, @"corrupt", @"matching-update"); [r checkBaseGame]; awaitCheck(r);
    assert([r->_lastTitle isEqual:@"GAME FILES NOT READY"]);
    r = fixture(root, nil, @"matching-update"); [r refreshInstallationFlow];
    assert(r->_installationStep == Theft4InstallationStepCopyBaseGame); assert(installedChecks == 0);
    // An update arriving during the asynchronous base check is recognized on completion.
    r = fixture(root, @"base", nil); patchArrivesDuringBaseCheck = YES;
    [r checkBaseGame]; awaitCheck(r); assertReady(r);
    // Foreground must not perform validation or dismiss an in-flight operation.
    for (int guard = 0; guard < 8; ++guard) {
        r = fixture(root, @"base", @"matching-update");
        switch (guard) {
        case 0: r->_loading = YES; break;
        case 1: r->_executionAttempted = YES; break;
        case 2: r->_saveTransferBusy = YES; break;
        case 3: r.presentedViewController = [NSObject new]; break;
        case 4: r->_installationStep = Theft4InstallationStepCreatingDirectory; break;
        case 5: r->_installationStep = Theft4InstallationStepCheckingBaseGame; break;
        case 6: r->_installationStep = Theft4InstallationStepInstallingUpdate; break;
        case 7: r->_failure = @"failure"; break;
        }
        [r refreshInstallationFlow]; assert(installedChecks == 0);
        assert(r->_installationStep != Theft4InstallationStepReady);
    }
    puts("PASS: existing pair, patched XEX, live copy, async recheck, invalid/missing files, idempotence and 8 busy guards");
} return 0; }
'''


class InstallationRoutingTests(unittest.TestCase):
    def test_actual_routing_methods(self):
        source = SOURCE.read_text()
        enum_start = source.index("typedef NS_ENUM(NSInteger, Theft4InstallationStep)")
        enum_end = source.index("\n};", enum_start) + 3
        signatures = [
            "- (BOOL)hasInstalledBaseAndUpdate", "- (BOOL)isSetupComplete",
            "- (void)markSetupComplete", "- (BOOL)completeSetupForInstalledGame",
            "- (void)refreshInstallationFlow", "- (void)presentInstallationFlowIfNeeded",
            "- (void)routeInstallationFlow", "- (void)checkBaseGame",
        ]
        harness = (PREAMBLE + source[enum_start:enum_end] + INTERFACE +
                   "\n".join(method(source, s) for s in signatures) + TESTS)
        with tempfile.TemporaryDirectory(prefix="theft4-install-routing-") as directory:
            path = Path(directory)
            (path / "routing.m").write_text(harness)
            subprocess.run(["xcrun", "clang", "-fobjc-arc", "-fblocks", "-Werror",
                            "-Wno-incompatible-pointer-types", "-framework", "Foundation",
                            str(path / "routing.m"), "-o", str(path / "routing")], check=True)
            subprocess.run([str(path / "routing"), directory], check=True, timeout=15)

    def test_lifecycle_and_picker_are_connected(self):
        source = SOURCE.read_text()
        for signature in ("- (void)activate", "- (void)viewDidAppear:(BOOL)animated"):
            self.assertIn("[self refreshInstallationFlow]", method(source, signature))
        choose = method(source, "- (void)chooseTitleUpdate")
        self.assertLess(choose.index("[self completeSetupForInstalledGame]"),
                        choose.index("UIDocumentPickerViewController *picker"))
        cancel = method(source, "- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller")
        self.assertIn("[self routeInstallationFlow]", cancel)


if __name__ == "__main__":
    unittest.main()
