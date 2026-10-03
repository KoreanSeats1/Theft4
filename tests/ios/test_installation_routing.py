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
static BOOL patchArrivesDuringBaseCheck, patchedXexArrivesDuringBaseCheck;
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
    if (patchedXexArrivesDuringBaseCheck) writeFile(@(path), @"default.xex", @"patched");
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
    installedChecks = baseChecks = 0;
    patchArrivesDuringBaseCheck = patchedXexArrivesDuringBaseCheck = NO;
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
    // Explicit Check Game Files after the prompt must recognize copied TU8.
    // This also covers prepatched installations with no separate update file.
    for (int prepatched = 0; prepatched < 2; ++prepatched) {
        r = fixture(root, nil, nil); [r refreshInstallationFlow];
        assert(r->_installationStep == Theft4InstallationStepCopyBaseGame);
        writeFile(r->_gameURL.path, @"default.xex", prepatched ? @"patched" : @"base");
        if (!prepatched) writeFile(r->_gameURL.path, @"default.xexp", @"matching-update");
        [r checkBaseGame]; assertReady(r); assert(baseChecks == 0);
    }
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
    r = fixture(root, @"incomplete-copy", nil); patchedXexArrivesDuringBaseCheck = YES;
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

COMPATIBILITY_DOUBLES = r'''
#define THEFT4_BC_TEXTURE_COMPATIBILITY 1
#define THEFT4_TEXTURE_PREPARATION_PREVIEW 1
static BOOL supportsBC = YES, cacheReady;
static int cacheChecks;
static BOOL Theft4DeviceNeedsBCTexturePreparation(void) { return !supportsBC; }
static NSString *Theft4TextureSourcesReviewedDefaultsKey(void) { return @"reviewed"; }
@interface SwitchDouble : NSObject
@property BOOL on;
@end
@implementation SwitchDouble
@end
@interface Theft4TexturePreparation : NSObject
@property BOOL running;
+ (BOOL)isCompleteForGame:(NSURL *)game support:(NSURL *)support;
@end
@implementation Theft4TexturePreparation
+ (BOOL)isCompleteForGame:(NSURL *)game support:(NSURL *)support {
    ++cacheChecks; return cacheReady;
}
@end
'''

COMPATIBILITY_CASES = r'''
    // Modern hardware must never consult the prepared cache or intercept Play.
    supportsBC = YES; cacheReady = NO; cacheChecks = 0;
    r = fixture(root, @"base", @"matching-update");
    [r refreshInstallationFlow]; assertReady(r); assert(cacheChecks == 0);
    [r startTransferredGame]; assert(r->_starts == 1); assert(cacheChecks == 0);
    // Missing preparation on unsupported hardware blocks Play and shows the notes.
    supportsBC = NO; cacheReady = NO; cacheChecks = 0;
    r = fixture(root, @"base", @"matching-update");
    [r refreshInstallationFlow]; assert(!r->_installationOverlay.hidden);
    assert(r->_installationStep == Theft4InstallationStepTextureSources);
    [r startTransferredGame]; assert(r->_starts == 0);
    // Going back to the launcher is allowed, but cannot bypass preparation at Play.
    r->_texturePreparationDeferred = YES;
    [r completeSetupForInstalledGame]; assertReady(r);
    [r startTransferredGame]; assert(r->_starts == 0);
    assert(r->_installationStep == Theft4InstallationStepTextureSources);
    // Completed preparation is reused and Play does not start another sweep.
    cacheReady = YES; cacheChecks = 0;
    r = fixture(root, @"base", @"matching-update");
    [r refreshInstallationFlow]; assertReady(r); assert(cacheChecks == 1);
    [r startTransferredGame]; assert(r->_starts == 1); assert(cacheChecks == 2);
    // Foreground/direct routing cannot replace progress or race cache deletion.
    for (int operation = 0; operation < 2; ++operation) {
        r = fixture(root, @"base", @"matching-update");
        r->_installationStep = Theft4InstallationStepTexturePreparing;
        r->_texturePreparation.running = operation == 0;
        r->_textureCacheDeleting = operation == 1;
        int priorChecks = cacheChecks;
        [r refreshInstallationFlow]; [r routeInstallationFlow]; [r checkBaseGame];
        assert(![r completeSetupForInstalledGame]); [r startTransferredGame];
        assert(r->_installationStep == Theft4InstallationStepTexturePreparing);
        assert(installedChecks == 0 && cacheChecks == priorChecks && r->_starts == 0);
    }
    puts("PASS: BC-capable launch bypass, required preparation, deferred Play, cache reuse and foreground busy guards");
'''


class InstallationRoutingTests(unittest.TestCase):
    def run_routing_harness(self, compatibility=False):
        source = SOURCE.read_text()
        enum_start = source.index("typedef NS_ENUM(NSInteger, Theft4InstallationStep)")
        enum_end = source.index("\n};", enum_start) + 3
        signatures = [
            "- (BOOL)hasInstalledBaseAndUpdate", "- (BOOL)isSetupComplete",
            "- (void)markSetupComplete", "- (BOOL)completeSetupForInstalledGame",
            "- (void)refreshInstallationFlow", "- (void)presentInstallationFlowIfNeeded",
            "- (void)routeInstallationFlow", "- (void)checkBaseGame",
        ]
        interface, tests = INTERFACE, TESTS
        if compatibility:
            interface = interface.replace("NSURL *_gameURL;", """NSURL *_gameURL, *_supportURL;
    SwitchDouble *_astcConversion;
    Theft4TexturePreparation *_texturePreparation;
    BOOL _texturePreparationDeferred, _textureCacheDeleting, _launcherDuringGame;
    int _starts;""")
            interface = interface.replace("- (void)refresh {}", """- (void)refresh {}
- (void)resumeGameFromLauncher {}
- (void)startGamePreparation:(NSURL *)game execute:(BOOL)execute { if (execute) ++_starts; }""")
            signatures.append("- (void)startTransferredGame")
            tests = tests.replace("installedChecks = baseChecks = 0;", """r->_supportURL = r->_gameURL;
    r->_astcConversion = [SwitchDouble new];
    r->_astcConversion.on = Theft4DeviceNeedsBCTexturePreparation();
    r->_texturePreparation = [Theft4TexturePreparation new];
    installedChecks = baseChecks = 0;""")
            tests = tests.replace('    puts("PASS: existing pair', COMPATIBILITY_CASES + '    puts("PASS: existing pair')
        harness = (PREAMBLE + (COMPATIBILITY_DOUBLES if compatibility else "") +
                   source[enum_start:enum_end] + interface +
                   "\n".join(method(source, s) for s in signatures) + tests)
        with tempfile.TemporaryDirectory(prefix="theft4-install-routing-") as directory:
            path = Path(directory)
            (path / "routing.m").write_text(harness)
            subprocess.run(["xcrun", "clang", "-fobjc-arc", "-fblocks", "-Werror",
                            "-Wno-incompatible-pointer-types", "-framework", "Foundation",
                            str(path / "routing.m"), "-o", str(path / "routing")], check=True)
            subprocess.run([str(path / "routing"), directory], check=True, timeout=15)

    def test_actual_routing_methods(self):
        self.run_routing_harness()

    def test_capability_gated_preparation_routing(self):
        self.run_routing_harness(compatibility=True)

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
