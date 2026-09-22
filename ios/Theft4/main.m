#import <UIKit/UIKit.h>
#import <GameController/GameController.h>
#import <QuartzCore/CAMetalLayer.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <os/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <stdint.h>
#include "theft4_core.h"
#include "theft4_device_profile.h"
#include "theft4_metal_presenter.h"
#include "theft4_lab_diagnostics.h"
#import "Theft4FrameTimeView.h"
extern int rex_gta4_native_profile_start(void);
extern int rex_gta4_native_profile_status(void);
#import "Theft4TouchControls.h"
#import "Theft4LauncherView.h"
#ifdef THEFT4_HAS_GAME_LOADER
#include "theft4_boot.h"
#endif

@interface Theft4MetalView : UIView
@end
@implementation Theft4MetalView
+ (Class)layerClass { return CAMetalLayer.class; }
@end

typedef NS_ENUM(NSInteger, Theft4InstallationStep) {
    Theft4InstallationStepNone,
    Theft4InstallationStepCreatingDirectory,
    Theft4InstallationStepCopyBaseGame,
    Theft4InstallationStepCheckingBaseGame,
    Theft4InstallationStepSelectingUpdate,
    Theft4InstallationStepInstallingUpdate,
    Theft4InstallationStepUpdateFailed,
    Theft4InstallationStepReady,
};

static NSString *Theft4DisplayName(void) {
    NSString *name = NSBundle.mainBundle.infoDictionary[@"CFBundleDisplayName"];
    return name.length ? name : @"Theft4";
}

static NSString *Theft4SetupCompleteDefaultsKey(void) {
#ifdef THEFT4_INTRO_TEST_BUILD
    return @"Theft4IntroTestSetupComplete";
#else
    return @"Theft4SetupComplete";
#endif
}

static NSString *Theft4InstallationDirectoryCreatedDefaultsKey(void) {
#ifdef THEFT4_INTRO_TEST_BUILD
    return @"Theft4IntroTestDirectoryCreated";
#else
    return @"Theft4InstallationDirectoryCreated";
#endif
}

static NSURL *Theft4GameDirectoryURL(NSURL *documents) {
#ifdef THEFT4_INTRO_TEST_BUILD
    NSURL *testRoot = [documents URLByAppendingPathComponent:@"intro-test" isDirectory:YES];
    return [testRoot URLByAppendingPathComponent:@"game" isDirectory:YES];
#else
    return [documents URLByAppendingPathComponent:@"game" isDirectory:YES];
#endif
}

static NSURL *Theft4InstructionsURL(NSURL *documents) {
#ifdef THEFT4_INTRO_TEST_BUILD
    NSURL *testRoot = [documents URLByAppendingPathComponent:@"intro-test" isDirectory:YES];
    return [testRoot URLByAppendingPathComponent:@"COPY GAME FILES HERE.txt"];
#else
    return [documents URLByAppendingPathComponent:@"COPY GAME FILES HERE.txt"];
#endif
}

static NSString *Theft4FilesGamePath(void) {
#ifdef THEFT4_INTRO_TEST_BUILD
    return [NSString stringWithFormat:@"On My iPhone/iPad → %@ → intro-test → game",
        Theft4DisplayName()];
#else
    return [NSString stringWithFormat:@"On My iPhone/iPad → %@ → game", Theft4DisplayName()];
#endif
}

static BOOL Theft4WriteBytes(NSOutputStream *stream, const uint8_t *bytes,
                             NSUInteger length, NSUInteger *budget) {
    if (!stream || !bytes || !budget || length > *budget) return NO;
    NSUInteger offset = 0;
    while (offset < length) {
        NSInteger written = [stream write:bytes + offset maxLength:length - offset];
        if (written <= 0) return NO;
        offset += (NSUInteger)written;
    }
    *budget -= length;
    return YES;
}

static BOOL Theft4WriteString(NSOutputStream *stream, NSString *value, NSUInteger *budget) {
    NSData *data = [value dataUsingEncoding:NSUTF8StringEncoding];
    return data && Theft4WriteBytes(stream, data.bytes, data.length, budget);
}

static BOOL Theft4AppendDiagnosticFile(NSOutputStream *stream, NSURL *url, NSString *label,
                                       NSUInteger *budget, NSMutableArray<NSString *> *included,
                                       NSMutableArray<NSString *> *skipped) {
    BOOL directory = NO;
    if (!url || ![NSFileManager.defaultManager fileExistsAtPath:url.path isDirectory:&directory] ||
        directory) return YES;
    NSDictionary *attributes = [NSFileManager.defaultManager attributesOfItemAtPath:url.path error:nil];
    unsigned long long fileSize = [attributes fileSize];
    if (fileSize > *budget) {
        [skipped addObject:[NSString stringWithFormat:@"%@ (%llu bytes; export budget exceeded)",
                             label, fileSize]];
        return YES;
    }
    NSString *header = [NSString stringWithFormat:@"\n\n===== %@ (%llu bytes) =====\n",
                        label, fileSize];
    NSData *headerData = [header dataUsingEncoding:NSUTF8StringEncoding];
    if (!headerData || headerData.length > *budget ||
        !Theft4WriteBytes(stream, headerData.bytes, headerData.length, budget)) return NO;
    NSInputStream *input = [NSInputStream inputStreamWithURL:url];
    [input open];
    uint8_t buffer[64 * 1024];
    BOOL success = YES;
    while (success) {
        NSInteger count = [input read:buffer maxLength:sizeof(buffer)];
        if (count < 0) { success = NO; break; }
        if (count == 0) break;
        success = Theft4WriteBytes(stream, buffer, (NSUInteger)count, budget);
    }
    [input close];
    if (success) [included addObject:label];
    return success;
}

static BOOL Theft4DiagnosticTextExtension(NSString *extension) {
    static NSSet<NSString *> *extensions;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        extensions = [NSSet setWithObjects:@"log", @"json", @"jsonl", @"csv", @"txt", @"partial", nil];
    });
    return [extensions containsObject:extension.lowercaseString];
}

static NSError *Theft4SaveError(NSString *message) {
    return [NSError errorWithDomain:@"Theft4SaveTransfer" code:1
                           userInfo:@{NSLocalizedDescriptionKey: message}];
}

// Only regular files and directories are accepted. A provider-supplied link must
// never redirect a copy out of the selected folder or into another app location.
static BOOL Theft4ValidateSaveTree(NSURL *root, NSError **error) {
    NSFileManager *fm = NSFileManager.defaultManager;
    NSNumber *isDirectory = nil, *isLink = nil;
    if (![root getResourceValue:&isDirectory forKey:NSURLIsDirectoryKey error:error] ||
        ![root getResourceValue:&isLink forKey:NSURLIsSymbolicLinkKey error:error] ||
        !isDirectory.boolValue || isLink.boolValue) {
        if (error && !*error) *error = Theft4SaveError(@"The save set is not a folder.");
        return NO;
    }
    __block NSError *scanFailure = nil;
    NSDirectoryEnumerator<NSURL *> *items = [fm enumeratorAtURL:root
        includingPropertiesForKeys:@[NSURLIsDirectoryKey, NSURLIsRegularFileKey,
                                     NSURLIsSymbolicLinkKey, NSURLFileSizeKey]
                           options:0 errorHandler:^BOOL(NSURL *url, NSError *scanError) {
        scanFailure = scanError ?: Theft4SaveError([NSString stringWithFormat:
            @"Could not read %@.", url.lastPathComponent]);
        return NO;
    }];
    if (!items) {
        if (error && !*error) *error = Theft4SaveError(@"Could not enumerate the save set.");
        return NO;
    }
    unsigned long long bytes = 0;
    NSUInteger count = 0;
    for (NSURL *item in items) {
        NSNumber *directory = nil, *regular = nil, *link = nil, *size = nil;
        if (![item getResourceValue:&directory forKey:NSURLIsDirectoryKey error:error] ||
            ![item getResourceValue:&regular forKey:NSURLIsRegularFileKey error:error] ||
            ![item getResourceValue:&link forKey:NSURLIsSymbolicLinkKey error:error] ||
            (regular.boolValue && ![item getResourceValue:&size forKey:NSURLFileSizeKey error:error])) return NO;
        if (link.boolValue || (!directory.boolValue && !regular.boolValue)) {
            if (error) *error = Theft4SaveError(@"The save set contains a link or unsupported file.");
            return NO;
        }
        bytes += size.unsignedLongLongValue;
        if (++count > 8192 || bytes > 1024ULL * 1024ULL * 1024ULL) {
            if (error) *error = Theft4SaveError(@"The save set exceeds the 1 GB / 8192-item safety limit.");
            return NO;
        }
    }
    if (scanFailure) {
        if (error) *error = scanFailure;
        return NO;
    }
    return error == nil || *error == nil;
}

static NSURL *Theft4SaveProfileURL(NSURL *support) {
    return [[[[support URLByAppendingPathComponent:@"user" isDirectory:YES]
        URLByAppendingPathComponent:@"545407F2" isDirectory:YES]
        URLByAppendingPathComponent:@"profile" isDirectory:YES] copy];
}

static NSURL *Theft4CreateSaveExport(NSURL *support, NSURL *documents, NSError **error) {
    NSFileManager *fm = NSFileManager.defaultManager;
    NSURL *saves = [support URLByAppendingPathComponent:@"saves" isDirectory:YES];
    NSURL *profile = Theft4SaveProfileURL(support);
    BOOL hasSaves = [fm fileExistsAtPath:saves.path];
    BOOL hasProfile = [fm fileExistsAtPath:profile.path];
    if (!hasSaves && !hasProfile) {
        if (error) *error = Theft4SaveError(@"No saves or GTA IV profile data exist yet.");
        return nil;
    }
    if ((hasSaves && !Theft4ValidateSaveTree(saves, error)) ||
        (hasProfile && !Theft4ValidateSaveTree(profile, error))) return nil;
    NSURL *exports = [documents URLByAppendingPathComponent:@"Save Exports" isDirectory:YES];
    if (![fm createDirectoryAtURL:exports withIntermediateDirectories:YES attributes:nil error:error]) return nil;
    NSDateFormatter *formatter = [NSDateFormatter new];
    formatter.dateFormat = @"yyyy-MM-dd-HHmmss";
    NSString *name = [NSString stringWithFormat:@"Theft4-Saves-%@-%@",
        [formatter stringFromDate:NSDate.date], [NSUUID.UUID.UUIDString substringToIndex:8]];
    NSURL *output = [exports URLByAppendingPathComponent:name isDirectory:YES];
    if (![fm createDirectoryAtURL:output withIntermediateDirectories:NO attributes:nil error:error]) return nil;
    BOOL success = (!hasSaves || [fm copyItemAtURL:saves
        toURL:[output URLByAppendingPathComponent:@"saves" isDirectory:YES] error:error]) &&
        (!hasProfile || [fm copyItemAtURL:profile
        toURL:[output URLByAppendingPathComponent:@"profile" isDirectory:YES] error:error]);
    if (success) {
        NSDictionary *manifest = @{@"format": @"theft4-save-export", @"version": @1,
            @"title_id": @"545407F2", @"saves": @(hasSaves), @"profile": @(hasProfile)};
        NSData *data = [NSJSONSerialization dataWithJSONObject:manifest options:NSJSONWritingPrettyPrinted error:error];
        success = data && [data writeToURL:[output URLByAppendingPathComponent:@"theft4-saves.json"]
                                  options:NSDataWritingAtomic error:error];
    }
    if (!success) { [fm removeItemAtURL:output error:nil]; return nil; }
    return output;
}

static BOOL Theft4ValidateSaveExport(NSURL *selected, BOOL *hasSaves, BOOL *hasProfile,
                                     NSError **error) {
    NSURL *manifestURL = [selected URLByAppendingPathComponent:@"theft4-saves.json"];
    NSNumber *regular = nil, *link = nil, *size = nil;
    if (![manifestURL getResourceValue:&regular forKey:NSURLIsRegularFileKey error:error] ||
        ![manifestURL getResourceValue:&link forKey:NSURLIsSymbolicLinkKey error:error] ||
        ![manifestURL getResourceValue:&size forKey:NSURLFileSizeKey error:error] ||
        !regular.boolValue || link.boolValue || size.unsignedLongLongValue > 65536) {
        if (error) *error = Theft4SaveError(@"The selected folder has no valid Theft4 save manifest.");
        return NO;
    }
    NSData *data = [NSData dataWithContentsOfURL:manifestURL options:0 error:error];
    NSDictionary *manifest = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:error] : nil;
    if (![manifest isKindOfClass:NSDictionary.class] ||
        ![manifest[@"format"] isEqual:@"theft4-save-export"] ||
        ![manifest[@"version"] isEqual:@1] ||
        ![manifest[@"title_id"] isEqual:@"545407F2"]) {
        if (error) *error = Theft4SaveError(@"Choose a Theft4-Saves export folder for GTA IV.");
        return NO;
    }
    *hasSaves = [manifest[@"saves"] boolValue];
    *hasProfile = [manifest[@"profile"] boolValue];
    if (!*hasSaves && !*hasProfile) {
        if (error) *error = Theft4SaveError(@"The selected save export is empty.");
        return NO;
    }
    return (!*hasSaves || Theft4ValidateSaveTree(
        [selected URLByAppendingPathComponent:@"saves" isDirectory:YES], error)) &&
        (!*hasProfile || Theft4ValidateSaveTree(
        [selected URLByAppendingPathComponent:@"profile" isDirectory:YES], error));
}

static BOOL Theft4InstallSaveExport(NSURL *selected, NSURL *support, NSURL *documents,
                                    NSURL **backupURL, NSError **error) {
    BOOL hasSaves = NO, hasProfile = NO;
    if (!Theft4ValidateSaveExport(selected, &hasSaves, &hasProfile, error)) return NO;
    NSFileManager *fm = NSFileManager.defaultManager;
    NSURL *staging = [support URLByAppendingPathComponent:
        [@"save-import-" stringByAppendingString:NSUUID.UUID.UUIDString] isDirectory:YES];
    if (![fm createDirectoryAtURL:staging withIntermediateDirectories:NO attributes:nil error:error])
        return NO;
    NSURL *stagedSaves = [staging URLByAppendingPathComponent:@"saves" isDirectory:YES];
    NSURL *stagedProfile = [staging URLByAppendingPathComponent:@"profile" isDirectory:YES];
    BOOL staged = (!hasSaves || [fm copyItemAtURL:
        [selected URLByAppendingPathComponent:@"saves" isDirectory:YES]
        toURL:stagedSaves error:error]) &&
        (!hasProfile || [fm copyItemAtURL:
        [selected URLByAppendingPathComponent:@"profile" isDirectory:YES]
        toURL:stagedProfile error:error]);
    if (!staged) { [fm removeItemAtURL:staging error:nil]; return NO; }

    NSURL *saves = [support URLByAppendingPathComponent:@"saves" isDirectory:YES];
    NSURL *profile = Theft4SaveProfileURL(support);
    BOOL oldSaves = hasSaves && [fm fileExistsAtPath:saves.path];
    BOOL oldProfile = hasProfile && [fm fileExistsAtPath:profile.path];
    if (oldSaves || oldProfile) {
        // Preserve a user-visible recovery copy before modifying either root.
        *backupURL = Theft4CreateSaveExport(support, documents, error);
        if (!*backupURL) { [fm removeItemAtURL:staging error:nil]; return NO; }
    }
    NSURL *rollbackSaves = [staging URLByAppendingPathComponent:@"old-saves" isDirectory:YES];
    NSURL *rollbackProfile = [staging URLByAppendingPathComponent:@"old-profile" isDirectory:YES];
    BOOL savedOldSaves = NO, savedOldProfile = NO;
    BOOL installedSaves = NO, installedProfile = NO;
    BOOL success = YES;
    if (oldSaves) success = savedOldSaves = [fm moveItemAtURL:saves toURL:rollbackSaves error:error];
    if (success && oldProfile)
        success = savedOldProfile = [fm moveItemAtURL:profile toURL:rollbackProfile error:error];
    if (success && hasSaves)
        success = installedSaves = [fm moveItemAtURL:stagedSaves toURL:saves error:error];
    if (success && hasProfile) {
        success = [fm createDirectoryAtURL:profile.URLByDeletingLastPathComponent
             withIntermediateDirectories:YES attributes:nil error:error];
        if (success) success = installedProfile = [fm moveItemAtURL:stagedProfile
                                                              toURL:profile error:error];
    }
    if (!success) {
        if (installedSaves) [fm removeItemAtURL:saves error:nil];
        if (installedProfile) [fm removeItemAtURL:profile error:nil];
        BOOL restored = YES;
        if (savedOldSaves) restored &= [fm moveItemAtURL:rollbackSaves toURL:saves error:nil];
        if (savedOldProfile) restored &= [fm moveItemAtURL:rollbackProfile toURL:profile error:nil];
        if (!restored && error) *error = Theft4SaveError(
            @"Import failed and automatic rollback was incomplete. Your backup is in Files → Theft4 → Save Exports.");
        else [fm removeItemAtURL:staging error:nil];
        return NO;
    }
    [fm removeItemAtURL:staging error:nil];
    return YES;
}

@interface Theft4ViewController : GCEventViewController <UIDocumentPickerDelegate> {
    theft4_core *_core;
    NSURL *_supportURL;
    NSURL *_logURL;
    UILabel *_status;
    UILabel *_detail;
    NSString *_failure;
    BOOL _selfTestRan;
    BOOL _bootRan;
    NSString *_bootStatus;
    BOOL _loading;
    UIButton *_prepare;
    UIButton *_start;
    BOOL _executionAttempted;
    Theft4MetalView *_metalView;
    Theft4LauncherView *_bringupOverlay;
    UILabel *_fpsLabel;
    NSTimer *_fpsTimer;
    uint64_t _fpsLastFrames;
    CFTimeInterval _fpsLastTime;
    UISwitch *_showFPS;
    UISwitch *_showFrameTime;
    Theft4FrameTimeView *_frameTimeView;
    NSTimer *_frameTimeTimer;
    NSLayoutConstraint *_frameTimeTop;
    BOOL _sceneActive;
    BOOL _nativeProfileRequested;
    BOOL _nativeProfileFinished;
    UISwitch *_showControls;
    UISwitch *_anisotropicFiltering;
    UISwitch *_enhancedOutput;
    UISwitch *_fsrBoost;
    UISwitch *_motionBlur;
    UISegmentedControl *_shadowQuality;
    UISegmentedControl *_drawDistance;
    UISegmentedControl *_modelDetail;
    UISegmentedControl *_reflectionQuality;
    UISegmentedControl *_antiAliasing;
    UISwitch *_performanceCapture;
    UIButton *_downloadLogButton;
    UIButton *_exportSavesButton;
    UIButton *_importSavesButton;
    UIDocumentPickerViewController *_saveImportPicker;
    BOOL _saveTransferBusy;
    Theft4TouchControls *_touchControls;
    BOOL _gamePresentation;
    BOOL _legacyIPadProfile;
    NSURL *_gameURL;
    UIView *_installationOverlay;
    UILabel *_installationTitle;
    UILabel *_installationDetail;
    UIActivityIndicatorView *_installationSpinner;
    UIButton *_installationAction;
    Theft4InstallationStep _installationStep;
    BOOL _installationFlowPresented;
    BOOL _baseGameChecked;
    BOOL _baseGameCheckAttempted;
    BOOL _setupValidated;
}
- (void)record:(NSString *)event;
- (void)activate;
- (void)pause;
- (void)shutdown;
- (void)bootEvent:(NSString *)event;
- (void)startGamePreparation:(NSURL *)game;
- (void)prepareTransferredGame;
- (void)startTransferredGame;
- (void)startGamePreparation:(NSURL *)game execute:(BOOL)execute;
- (void)enterGamePresentationMode;
- (void)initializeSharedGameDirectory;
- (BOOL)hasInstalledBaseAndUpdate;
- (BOOL)isSetupComplete;
- (void)markSetupComplete;
- (void)presentInstallationFlowIfNeeded;
- (void)routeInstallationFlow;
- (void)checkBaseGame;
- (void)chooseTitleUpdate;
- (void)downloadLatestLogCapture;
- (void)exportSavesToFiles;
- (void)importSavesFromFiles;
- (void)showSaveMessage:(NSString *)title detail:(NSString *)detail;
- (void)updateFrameTimeHUD;
- (void)applyLowPowerPreset;
- (void)syncA19OutputChoice;
#ifdef THEFT4_INTRO_TEST_BUILD
- (void)runIntroTestImportSmokeIfRequested;
#endif
@end

static void coreEvent(void *context, const char *event) {
    Theft4ViewController *controller = (__bridge Theft4ViewController *)context;
    [controller record:[NSString stringWithUTF8String:event]];
}

static BOOL configureDeviceProfile(void) {
    const char *configured = getenv("THEFT4_DEVICE_PROFILE");
    if (configured) return strcmp(configured, "legacy-ipad") == 0;

    struct utsname systemInfo = {};
    const char *machine = uname(&systemInfo) == 0 ? systemInfo.machine : "unknown";
    // Apple's A19 iPhone family uses the iPhone18,* hardware identifiers. This
    // selects launch defaults only; it does not enable a new instruction set.
    const BOOL isA19 = strncmp(machine, "iPhone18,", 9) == 0;
    const BOOL isIPad = UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad;
    const uint64_t physicalMemory = NSProcessInfo.processInfo.physicalMemory;
    const char *profile = theft4_device_profile_name(isIPad, physicalMemory, isA19);
    setenv("THEFT4_DEVICE_PROFILE", profile, 0);
    setenv("THEFT4_DEVICE_MODEL", machine, 0);
    os_log(OS_LOG_DEFAULT,
           "Theft4 launch profile %{public}s for %{public}s (%{public}llu MiB)",
           profile, machine, (unsigned long long)(physicalMemory >> 20));
    return strcmp(profile, "legacy-ipad") == 0;
}

#ifdef THEFT4_HAS_GAME_LOADER
static void bootEvent(void *context, const char *event) {
    fprintf(stderr, "Theft4 loader: %s\n", event);
    fflush(stderr);
    Theft4ViewController *controller = (__bridge Theft4ViewController *)context;
    NSString *message = [NSString stringWithUTF8String:event];
    dispatch_async(dispatch_get_main_queue(), ^{ [controller bootEvent:message]; });
}
#endif

@implementation Theft4ViewController
- (void)viewDidLoad {
    [super viewDidLoad];
    self.controllerUserInteractionEnabled = YES;
    _legacyIPadProfile = configureDeviceProfile();
    [NSUserDefaults.standardUserDefaults registerDefaults:@{
        @"Theft4ShowFPS": @YES,
        @"Theft4ShowFrameTime": @NO,
        @"Theft4ShowTouchControls": @NO,
        @"Theft4AnisotropicFiltering": @(!_legacyIPadProfile),
        @"Theft4EnhancedOutput1080p": @(!_legacyIPadProfile),
        @"Theft4ExperimentalFSRBoost": @NO,
        @"Theft4MotionBlur": @NO,
        @"Theft4ShadowQuality": @0,
        @"Theft4DrawDistance": @0,
        @"Theft4ModelDetail": @0,
        @"Theft4ReflectionQuality": @0,
        @"Theft4AntiAliasing": @2,
        @"Theft4DetailedPerformanceCapture": @NO
    }];
    // The native game image is 1280x720. Keep its layer itself at 16:9 so
    // MoltenVK's kCAGravityResize policy can't stretch it to the iPad aspect.
    self.view.backgroundColor = UIColor.blackColor;
    _metalView = [Theft4MetalView new];
    _metalView.translatesAutoresizingMaskIntoConstraints = NO;
    _metalView.userInteractionEnabled = NO;
    [self.view addSubview:_metalView];
    [NSLayoutConstraint activateConstraints:@[
        [_metalView.centerXAnchor constraintEqualToAnchor:self.view.centerXAnchor],
        [_metalView.centerYAnchor constraintEqualToAnchor:self.view.centerYAnchor],
        [_metalView.widthAnchor constraintEqualToAnchor:_metalView.heightAnchor multiplier:(16.0 / 9.0)],
        [_metalView.widthAnchor constraintLessThanOrEqualToAnchor:self.view.widthAnchor],
        [_metalView.heightAnchor constraintLessThanOrEqualToAnchor:self.view.heightAnchor]
    ]];
    NSLayoutConstraint *preferFullWidth =
        [_metalView.widthAnchor constraintEqualToAnchor:self.view.widthAnchor];
    preferFullWidth.priority = 999;
    preferFullWidth.active = YES;
    theft4_metal_bind_layer((__bridge void *)_metalView.layer);
    _bringupOverlay = [Theft4LauncherView new];
    _bringupOverlay.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_bringupOverlay];
    [NSLayoutConstraint activateConstraints:@[
        [_bringupOverlay.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [_bringupOverlay.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
        [_bringupOverlay.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [_bringupOverlay.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor]
    ]];
    _status = _bringupOverlay.statusLabel; _detail = _bringupOverlay.detailLabel;
    _start = _bringupOverlay.startButton; _prepare = _bringupOverlay.prepareButton;
    [_start addTarget:self action:@selector(startTransferredGame) forControlEvents:UIControlEventTouchUpInside];
    [_prepare addTarget:self action:@selector(prepareTransferredGame) forControlEvents:UIControlEventTouchUpInside];
    [_bringupOverlay.restartButton addTarget:self action:@selector(restartCore) forControlEvents:UIControlEventTouchUpInside];
    [_bringupOverlay.lowPowerButton addTarget:self action:@selector(applyLowPowerPreset)
        forControlEvents:UIControlEventTouchUpInside];
#ifndef THEFT4_HAS_GAME_STARTUP
    _start.hidden = YES;
#endif
    _showFrameTime = _bringupOverlay.showFrameTime;
    _showFPS = _bringupOverlay.showFPS; _showControls = _bringupOverlay.showControls;
    _anisotropicFiltering = _bringupOverlay.anisotropicFiltering;
    _enhancedOutput = _bringupOverlay.enhancedOutput; _fsrBoost = _bringupOverlay.fsrBoost;
    _motionBlur = _bringupOverlay.motionBlur;
    _shadowQuality = _bringupOverlay.shadowQuality;
    _drawDistance = _bringupOverlay.drawDistance;
    _modelDetail = _bringupOverlay.modelDetail;
    _reflectionQuality = _bringupOverlay.reflectionQuality;
    _antiAliasing = _bringupOverlay.antiAliasing;
    _performanceCapture = _bringupOverlay.performanceCapture;
    _downloadLogButton = _bringupOverlay.downloadLogButton;
    [_downloadLogButton addTarget:self action:@selector(downloadLatestLogCapture)
                 forControlEvents:UIControlEventTouchUpInside];
    _exportSavesButton = _bringupOverlay.exportSavesButton;
    _importSavesButton = _bringupOverlay.importSavesButton;
    [_exportSavesButton addTarget:self action:@selector(exportSavesToFiles)
                forControlEvents:UIControlEventTouchUpInside];
    [_importSavesButton addTarget:self action:@selector(importSavesFromFiles)
                forControlEvents:UIControlEventTouchUpInside];
    NSArray *toggles = @[_showFrameTime,_showFPS,_showControls,_anisotropicFiltering,_enhancedOutput,_fsrBoost,
                         _motionBlur,_performanceCapture];
    NSArray *keys = @[@"Theft4ShowFrameTime",@"Theft4ShowFPS",@"Theft4ShowTouchControls",@"Theft4AnisotropicFiltering",
                      @"Theft4EnhancedOutput1080p",@"Theft4ExperimentalFSRBoost",@"Theft4MotionBlur",
                      @"Theft4DetailedPerformanceCapture"];
    for (NSUInteger i=0;i<toggles.count;++i) {
        UISwitch *toggle = toggles[i];
        toggle.on = [NSUserDefaults.standardUserDefaults boolForKey:keys[i]];
        [toggle addTarget:self action:@selector(displaySettingsChanged:) forControlEvents:UIControlEventValueChanged];
    }
    NSArray<UISegmentedControl *> *graphicsChoices = @[
        _shadowQuality, _drawDistance, _modelDetail, _reflectionQuality, _antiAliasing];
    NSArray<NSString *> *graphicsKeys = @[
        @"Theft4ShadowQuality", @"Theft4DrawDistance", @"Theft4ModelDetail",
        @"Theft4ReflectionQuality", @"Theft4AntiAliasing"];
    for (NSUInteger i = 0; i < graphicsChoices.count; ++i) {
        UISegmentedControl *choice = graphicsChoices[i];
        choice.selectedSegmentIndex = MAX(0, MIN(choice.numberOfSegments - 1,
            [NSUserDefaults.standardUserDefaults integerForKey:graphicsKeys[i]]));
        [choice addTarget:self action:@selector(displaySettingsChanged:)
            forControlEvents:UIControlEventValueChanged];
    }
    if (_bringupOverlay.renderResolution) {
        NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
        uint32_t height = theft4_lab_render_height((uint32_t)[defaults integerForKey:@"Theft4LabRenderHeight"]);
        _bringupOverlay.renderResolution.selectedSegmentIndex =
            height == 540 ? 0 : height == 900 ? 2 : height == 1080 ? 3 : 1;
        if (![defaults objectForKey:@"Theft4LabFSREnabled"])
            [defaults setBool:_enhancedOutput.on forKey:@"Theft4LabFSREnabled"];
        _bringupOverlay.fsrUpscaling.on = [defaults boolForKey:@"Theft4LabFSREnabled"];
        [self syncA19OutputChoice];
        [_bringupOverlay.renderResolution addTarget:self action:@selector(displaySettingsChanged:)
            forControlEvents:UIControlEventValueChanged];
        [_bringupOverlay.fsrUpscaling addTarget:self action:@selector(displaySettingsChanged:)
            forControlEvents:UIControlEventValueChanged];
    }
    // Older installs may have M-series defaults persisted. Start pre-M iPads
    // in a conservative mode on every process launch, while still allowing an
    // explicit user opt-in before starting the game.
    if (_legacyIPadProfile) {
        _anisotropicFiltering.on = NO;
        _enhancedOutput.on = NO;
        _fsrBoost.on = NO;
        _motionBlur.on = NO;
    }
    if (_fsrBoost.on) _enhancedOutput.on = YES;
    [_bringupOverlay refreshConfigurationSummary];
    _touchControls = [Theft4TouchControls new];
    _touchControls.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_touchControls];
    [NSLayoutConstraint activateConstraints:@[
        [_touchControls.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [_touchControls.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
        [_touchControls.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [_touchControls.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor]
    ]];
    _fpsLabel = [UILabel new];
    _fpsLabel.translatesAutoresizingMaskIntoConstraints = NO;
    _fpsLabel.text = @"--.- FPS";
    _fpsLabel.textAlignment = NSTextAlignmentCenter;
    _fpsLabel.font = [UIFont monospacedDigitSystemFontOfSize:15 weight:UIFontWeightSemibold];
    _fpsLabel.textColor = UIColor.whiteColor;
    _fpsLabel.backgroundColor = [UIColor colorWithWhite:0.0 alpha:0.58];
    _fpsLabel.layer.cornerRadius = 7.0;
    _fpsLabel.layer.masksToBounds = YES;
    _fpsLabel.hidden = YES;
    _fpsLabel.accessibilityIdentifier = @"game.fps";
    [self.view addSubview:_fpsLabel];
    [NSLayoutConstraint activateConstraints:@[
        [_fpsLabel.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor constant:10],
        [_fpsLabel.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor constant:-10],
        [_fpsLabel.widthAnchor constraintEqualToConstant:92],
        [_fpsLabel.heightAnchor constraintEqualToConstant:32]
    ]];
    _frameTimeView = [Theft4FrameTimeView new];
    _frameTimeView.translatesAutoresizingMaskIntoConstraints = NO;
    _frameTimeView.hidden = YES;
    UITapGestureRecognizer *capture = [[UITapGestureRecognizer alloc]
        initWithTarget:self action:@selector(requestNativeProfile)];
    capture.numberOfTapsRequired = 2;
    [_frameTimeView addGestureRecognizer:capture];
    [self.view addSubview:_frameTimeView];
    _frameTimeTop = [_frameTimeView.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor constant:52];
    [NSLayoutConstraint activateConstraints:@[_frameTimeTop,
        [_frameTimeView.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor constant:-10],
        [_frameTimeView.widthAnchor constraintEqualToConstant:244],
        [_frameTimeView.heightAnchor constraintEqualToConstant:126]]];
    _fpsTimer = [NSTimer scheduledTimerWithTimeInterval:0.5
                                                target:self
                                              selector:@selector(refreshFrameRate)
                                              userInfo:nil
                                               repeats:YES];
    NSError *error = nil;
    NSURL *support = [NSFileManager.defaultManager URLForDirectory:NSApplicationSupportDirectory
        inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:&error];
    _supportURL = [support URLByAppendingPathComponent:@"Theft4" isDirectory:YES];
    if (!_supportURL || ![NSFileManager.defaultManager createDirectoryAtURL:_supportURL
        withIntermediateDirectories:YES attributes:nil error:&error]) {
        _failure = error.localizedDescription ?: @"Application Support is unavailable";
    } else {
        _logURL = [_supportURL URLByAppendingPathComponent:@"lifecycle.jsonl"];
    }
    [self initializeSharedGameDirectory];
    [self record:@"app.probe_loaded"];
    [self createCore];
#ifdef THEFT4_INTRO_TEST_BUILD
    [self runIntroTestImportSmokeIfRequested];
#endif
}

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    if ([self isSetupComplete]) {
        _installationOverlay.hidden = YES;
        return;
    }
    if (_installationFlowPresented && _installationStep == Theft4InstallationStepCopyBaseGame) {
        [self routeInstallationFlow];
    } else {
        [self presentInstallationFlowIfNeeded];
    }
}

- (void)initializeSharedGameDirectory {
    NSError *error = nil;
    NSURL *documents = [NSFileManager.defaultManager URLForDirectory:NSDocumentDirectory
        inDomain:NSUserDomainMask appropriateForURL:nil create:NO error:&error];
    if (!documents) {
        _failure = [NSString stringWithFormat:@"Cannot access the app Documents folder: %@",
            error.localizedDescription ?: @"Documents is unavailable"];
        return;
    }
    _gameURL = Theft4GameDirectoryURL(documents);
    if ([self hasInstalledBaseAndUpdate]) {
        // Migration for an already-configured app: only inspect the existing
        // files, then record completion in this app's private preferences.
        // Do not create, rewrite, move, or import anything in Documents.
        [self markSetupComplete];
        _bootStatus = @"Game files detected. Ready to play.";
        return;
    }
    if (![NSFileManager.defaultManager createDirectoryAtURL:_gameURL
        withIntermediateDirectories:YES attributes:nil error:&error]) {
        _failure = [NSString stringWithFormat:@"Cannot create the shared game folder: %@",
            error.localizedDescription ?: @"Documents is unavailable"];
        return;
    }

    NSURL *instructionsURL = Theft4InstructionsURL(documents);
    NSString *instructions = [NSString stringWithFormat:
        @"%@ game-file transfer\n\n"
        @"Copy the CONTENTS of your legally obtained, extracted Xbox 360 GTA IV game "
        @"folder into the game folder next to this file. The folder must contain "
        @"game/default.xex directly; do not create game/game and do not copy a raw ISO.\n\n"
        @"Return to %@ after the copy finishes. The app will ask you to select the "
        @"matching title-update file and install it for you.\n", Theft4DisplayName(), Theft4DisplayName()];
    if (![NSFileManager.defaultManager fileExistsAtPath:instructionsURL.path] &&
        ![instructions writeToURL:instructionsURL atomically:YES
        encoding:NSUTF8StringEncoding error:&error]) {
        _failure = [NSString stringWithFormat:@"Cannot create transfer instructions: %@",
            error.localizedDescription ?: @"write failed"];
        return;
    }

    [_gameURL setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:nil];
    BOOL hasBase = [NSFileManager.defaultManager
        fileExistsAtPath:[[_gameURL URLByAppendingPathComponent:@"default.xex"] path]];
    BOOL hasUpdate = [NSFileManager.defaultManager
        fileExistsAtPath:[[_gameURL URLByAppendingPathComponent:@"default.xexp"] path]];
    _bootStatus = hasBase && hasUpdate
        ? @"Game files detected. Ready to play."
        : (hasBase ? @"Base game detected. Select the title update to continue."
                   : [NSString stringWithFormat:@"Transfer folder ready: Files → %@",
                        Theft4FilesGamePath()]);
}

- (BOOL)hasInstalledBaseAndUpdate {
    if (!_gameURL) return NO;
    const BOOL hasBase = [NSFileManager.defaultManager
        fileExistsAtPath:[[_gameURL URLByAppendingPathComponent:@"default.xex"] path]];
    const BOOL hasUpdate = [NSFileManager.defaultManager
        fileExistsAtPath:[[_gameURL URLByAppendingPathComponent:@"default.xexp"] path]];
    if (!hasBase || !hasUpdate) {
        _setupValidated = NO;
        return NO;
    }
#ifdef THEFT4_HAS_GAME_LOADER
    char message[1024] = {};
    _setupValidated = theft4_validate_installed_game(_gameURL.fileSystemRepresentation,
        message, sizeof(message)) == 0;
    return _setupValidated;
#else
    return NO;
#endif
}

- (BOOL)isSetupComplete {
    return [NSUserDefaults.standardUserDefaults boolForKey:Theft4SetupCompleteDefaultsKey()] &&
        [self hasInstalledBaseAndUpdate];
}

- (void)markSetupComplete {
    [NSUserDefaults.standardUserDefaults setBool:YES forKey:Theft4SetupCompleteDefaultsKey()];
}

- (void)createInstallationOverlayIfNeeded {
    if (_installationOverlay) return;
    _installationOverlay = [UIView new];
    _installationOverlay.translatesAutoresizingMaskIntoConstraints = NO;
    _installationOverlay.backgroundColor = [UIColor colorWithRed:0.043 green:0.078 blue:0.094 alpha:0.985];
    [self.view addSubview:_installationOverlay];
    [NSLayoutConstraint activateConstraints:@[
        [_installationOverlay.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [_installationOverlay.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
        [_installationOverlay.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [_installationOverlay.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
    ]];

    UILabel *eyebrow = [UILabel new];
    eyebrow.text = @"THEFT4  /  SETUP";
    eyebrow.font = [UIFont monospacedSystemFontOfSize:12 weight:UIFontWeightSemibold];
    eyebrow.textColor = [UIColor colorWithRed:0.90 green:0.71 blue:0.42 alpha:1.0];
    _installationTitle = [UILabel new];
    _installationTitle.numberOfLines = 0;
    _installationTitle.font = [UIFont systemFontOfSize:30 weight:UIFontWeightBold];
    _installationTitle.textColor = [UIColor colorWithRed:0.93 green:0.91 blue:0.84 alpha:1.0];
    _installationDetail = [UILabel new];
    _installationDetail.numberOfLines = 0;
    _installationDetail.font = [UIFont systemFontOfSize:16 weight:UIFontWeightRegular];
    _installationDetail.textColor = [UIColor colorWithRed:0.67 green:0.72 blue:0.71 alpha:1.0];
    _installationSpinner = [[UIActivityIndicatorView alloc] initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleLarge];
    _installationSpinner.color = [UIColor colorWithRed:0.90 green:0.71 blue:0.42 alpha:1.0];
    _installationAction = [UIButton buttonWithType:UIButtonTypeSystem];
    UIButtonConfiguration *configuration = [UIButtonConfiguration filledButtonConfiguration];
    configuration.cornerStyle = UIButtonConfigurationCornerStyleMedium;
    configuration.baseForegroundColor = [UIColor colorWithRed:0.07 green:0.11 blue:0.12 alpha:1.0];
    configuration.baseBackgroundColor = [UIColor colorWithRed:0.90 green:0.71 blue:0.42 alpha:1.0];
    configuration.contentInsets = NSDirectionalEdgeInsetsMake(16, 18, 16, 18);
    _installationAction.configuration = configuration;
    _installationAction.titleLabel.font = [UIFont systemFontOfSize:16 weight:UIFontWeightBold];
    [_installationAction addTarget:self action:@selector(handleInstallationAction)
                   forControlEvents:UIControlEventTouchUpInside];

    UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:@[
        eyebrow, _installationTitle, _installationDetail, _installationSpinner, _installationAction
    ]];
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    stack.axis = UILayoutConstraintAxisVertical;
    stack.spacing = 20;
    stack.alignment = UIStackViewAlignmentFill;
    [_installationOverlay addSubview:stack];
    NSLayoutConstraint *preferredWidth = [stack.widthAnchor
        constraintEqualToAnchor:_installationOverlay.safeAreaLayoutGuide.widthAnchor constant:-60];
    preferredWidth.priority = UILayoutPriorityDefaultHigh;
    [NSLayoutConstraint activateConstraints:@[
        [stack.centerXAnchor constraintEqualToAnchor:_installationOverlay.centerXAnchor],
        [stack.centerYAnchor constraintEqualToAnchor:_installationOverlay.centerYAnchor],
        [stack.widthAnchor constraintLessThanOrEqualToConstant:480],
        preferredWidth,
        [_installationAction.heightAnchor constraintGreaterThanOrEqualToConstant:54],
    ]];
}

- (void)showInstallationTitle:(NSString *)title detail:(NSString *)detail
                    actionTitle:(NSString *)actionTitle spinner:(BOOL)spinner
                           step:(Theft4InstallationStep)step {
    [self createInstallationOverlayIfNeeded];
    _installationStep = step;
    _installationOverlay.hidden = NO;
    _installationTitle.text = title;
    _installationDetail.text = detail;
    _installationSpinner.hidden = !spinner;
    if (spinner) [_installationSpinner startAnimating];
    else [_installationSpinner stopAnimating];
    _installationAction.hidden = actionTitle.length == 0;
    _installationAction.enabled = actionTitle.length > 0;
    UIButtonConfiguration *configuration = _installationAction.configuration;
    configuration.title = actionTitle;
    _installationAction.configuration = configuration;
}

- (void)presentInstallationFlowIfNeeded {
    if (_installationFlowPresented || _failure || !_gameURL) return;
    if ([self isSetupComplete]) return;
    _installationFlowPresented = YES;
    NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
    const BOOL firstLaunch = ![defaults boolForKey:Theft4InstallationDirectoryCreatedDefaultsKey()];
    [defaults setBool:YES forKey:Theft4InstallationDirectoryCreatedDefaultsKey()];
    if (!firstLaunch) {
        [self routeInstallationFlow];
        return;
    }

    [self showInstallationTitle:@"CREATING DIRECTORY"
                         detail:[NSString stringWithFormat:@"Preparing %@ for your game files…",
                             Theft4FilesGamePath()]
                    actionTitle:nil spinner:YES step:Theft4InstallationStepCreatingDirectory];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3 * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
        if (self->_installationStep == Theft4InstallationStepCreatingDirectory)
            [self routeInstallationFlow];
    });
}

- (void)routeInstallationFlow {
    if (!_gameURL) return;
    const BOOL hasBase = [NSFileManager.defaultManager
        fileExistsAtPath:[[_gameURL URLByAppendingPathComponent:@"default.xex"] path]];
    if (!hasBase) {
        NSString *title = _baseGameCheckAttempted ? @"GAME FILES NOT FOUND"
                                                   : @"QUIT APP & LOAD GAME FILES";
        NSString *detail = _baseGameCheckAttempted
            ? [NSString stringWithFormat:@"No default.xex was found in:\n\n%@\n\nCopy the extracted game contents there, then check again.",
                Theft4FilesGamePath()]
            : [NSString stringWithFormat:@"In Files, copy the contents of your legally obtained, extracted Xbox 360 GTA IV game into:\n\n%@\n\nThe folder must contain default.xex directly. Do not copy a raw ISO. When the transfer finishes, reopen %@.",
                Theft4FilesGamePath(), Theft4DisplayName()];
        [self showInstallationTitle:title detail:detail
                        actionTitle:_baseGameCheckAttempted ? @"CHECK AGAIN" : @"CHECK FOR GAME FILES" spinner:NO
                               step:Theft4InstallationStepCopyBaseGame];
        return;
    }
    if (!_baseGameChecked) {
        [self showInstallationTitle:@"GAME FILES DETECTED"
                             detail:@"The extracted base game was found. Check it before selecting a title update; this check reads the files and does not change them."
                        actionTitle:@"CHECK GAME FILES" spinner:NO
                               step:Theft4InstallationStepCopyBaseGame];
        return;
    }
    [self showInstallationTitle:@"SELECT TITLE UPDATE"
                         detail:@"Your base game is verified. Select the matching GTA IV Xbox 360 title-update file from Files. Theft4 will validate it, extract it, and install it automatically."
                    actionTitle:@"SELECT TITLE UPDATE" spinner:NO
                           step:Theft4InstallationStepSelectingUpdate];
}

- (void)handleInstallationAction {
    switch (_installationStep) {
        case Theft4InstallationStepCopyBaseGame:
            [self checkBaseGame];
            break;
        case Theft4InstallationStepSelectingUpdate:
        case Theft4InstallationStepUpdateFailed:
            [self chooseTitleUpdate];
            break;
        case Theft4InstallationStepReady:
            [self markSetupComplete];
            _installationOverlay.hidden = YES;
            _bootStatus = @"Game files detected. Ready to play.";
            [self record:@"install.ready"];
            [self refresh];
            break;
        default:
            break;
    }
}

- (void)checkBaseGame {
    if (!_gameURL) return;
    const BOOL hasBase = [NSFileManager.defaultManager
        fileExistsAtPath:[[_gameURL URLByAppendingPathComponent:@"default.xex"] path]];
    _baseGameCheckAttempted = YES;
    if (!hasBase) {
        [self routeInstallationFlow];
        return;
    }

    [self showInstallationTitle:@"CHECKING GAME FILES"
                         detail:@"Validating the extracted GTA IV base game without changing it…"
                    actionTitle:nil spinner:YES step:Theft4InstallationStepCheckingBaseGame];
    NSURL *game = _gameURL;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
#ifdef THEFT4_HAS_GAME_LOADER
        char message[1024] = {};
        const int result = theft4_validate_base_game(game.fileSystemRepresentation,
            message, sizeof(message));
        NSString *detail = [NSString stringWithUTF8String:message];
#else
        const int result = 1;
        NSString *detail = @"This build does not include the game-file validator.";
#endif
        dispatch_async(dispatch_get_main_queue(), ^{
            if (result == 0) {
                self->_baseGameChecked = YES;
                [self routeInstallationFlow];
                return;
            }
            [self showInstallationTitle:@"GAME FILES NOT READY"
                                 detail:detail.length ? detail : @"The extracted base game could not be verified."
                            actionTitle:@"CHECK AGAIN" spinner:NO
                                   step:Theft4InstallationStepCopyBaseGame];
        });
    });
}

#ifdef THEFT4_INTRO_TEST_BUILD
- (void)runIntroTestImportSmokeIfRequested {
    if (![NSProcessInfo.processInfo.arguments containsObject:@"--theft4-intro-import-smoke"] ||
        !_gameURL) return;
    _installationFlowPresented = YES;
    NSURL *testRoot = [_gameURL URLByDeletingLastPathComponent];
    NSURL *source = [[testRoot URLByAppendingPathComponent:@"title-update-source" isDirectory:YES]
        URLByAppendingPathComponent:@"default.xexp"];
    if (![NSFileManager.defaultManager fileExistsAtPath:source.path]) {
        _bootStatus = @"Intro importer smoke test failed: default.xexp was not staged.";
        [self record:@"intro_test.raw_patch_import_failed"];
        return;
    }
    [self showInstallationTitle:@"RUNNING IMPORTER TEST"
                         detail:@"Checking the isolated raw title-update install…"
                    actionTitle:nil spinner:YES step:Theft4InstallationStepInstallingUpdate];
    NSURL *game = _gameURL;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        char base_message[1024] = {};
        const int base_validated = theft4_validate_base_game(game.fileSystemRepresentation,
            base_message, sizeof(base_message));
        char import_message[1024] = {};
        const int imported = base_validated == 0 ? theft4_install_title_update(
            game.fileSystemRepresentation, source.fileSystemRepresentation,
            import_message, sizeof(import_message)) : 1;
        char verify_message[1024] = {};
        const int verified = imported == 0 ? theft4_validate_installed_game(
            game.fileSystemRepresentation, verify_message, sizeof(verify_message)) : 1;
        NSString *detail = [NSString stringWithUTF8String:
            base_validated != 0 ? base_message : (imported == 0 ? verify_message : import_message)];
        dispatch_async(dispatch_get_main_queue(), ^{
            if (base_validated == 0 && imported == 0 && verified == 0) {
                self->_bootStatus = @"Intro importer smoke test passed.";
                self->_setupValidated = YES;
                [self record:@"intro_test.raw_patch_import_passed"];
                [self showInstallationTitle:@"IMPORTER TEST PASSED"
                                     detail:detail.length ? detail : @"The raw title update was installed and verified."
                                actionTitle:@"OPEN MAIN SCREEN" spinner:NO
                                       step:Theft4InstallationStepReady];
            } else {
                self->_bootStatus = @"Intro importer smoke test failed.";
                [self record:@"intro_test.raw_patch_import_failed"];
                [self showInstallationTitle:@"IMPORTER TEST FAILED"
                                     detail:detail.length ? detail : @"The raw title update could not be installed."
                                actionTitle:nil spinner:NO step:Theft4InstallationStepUpdateFailed];
            }
        });
    });
}
#endif

- (void)showSaveMessage:(NSString *)title detail:(NSString *)detail {
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:title
        message:detail preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK"
        style:UIAlertActionStyleDefault handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

- (void)exportSavesToFiles {
    if (!_supportURL || _loading || _gamePresentation || _saveTransferBusy) {
        [self showSaveMessage:@"CLOSE THE GAME FIRST"
                     detail:@"Quit and reopen Theft4 before transferring saves."];
        return;
    }
    NSURL *documents = [NSFileManager.defaultManager URLForDirectory:NSDocumentDirectory
        inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:nil];
    if (!documents) {
        [self showSaveMessage:@"SAVE EXPORT FAILED" detail:@"The Files folder is unavailable."];
        return;
    }
    _saveTransferBusy = YES;
    _exportSavesButton.enabled = NO;
    _importSavesButton.enabled = NO;
    NSURL *support = [_supportURL copy];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSError *error = nil;
        NSURL *output = Theft4CreateSaveExport(support, documents, &error);
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_exportSavesButton.enabled = YES;
            self->_importSavesButton.enabled = YES;
            self->_saveTransferBusy = NO;
            if (output) {
                [self record:@"saves.export_completed"];
                [self showSaveMessage:@"SAVES EXPORTED"
                             detail:[NSString stringWithFormat:
                    @"Open Files → On My iPhone/iPad → Theft4 → Save Exports → %@. Copy this entire folder to your backup location.",
                    output.lastPathComponent]];
            } else {
                [self record:@"saves.export_failed"];
                [self showSaveMessage:@"SAVE EXPORT FAILED"
                             detail:error.localizedDescription ?: @"Could not copy the save data."];
            }
        });
    });
}

- (void)importSavesFromFiles {
    if (!_supportURL || _loading || _gamePresentation || _saveTransferBusy) {
        [self showSaveMessage:@"CLOSE THE GAME FIRST"
                     detail:@"Quit and reopen Theft4 before transferring saves."];
        return;
    }
    _saveImportPicker = [[UIDocumentPickerViewController alloc]
        initForOpeningContentTypes:@[UTTypeFolder] asCopy:NO];
    _saveImportPicker.delegate = self;
    _saveImportPicker.allowsMultipleSelection = NO;
    [self presentViewController:_saveImportPicker animated:YES completion:nil];
}

- (void)chooseTitleUpdate {
    if (!_gameURL) return;
    const BOOL hasBase = [NSFileManager.defaultManager
        fileExistsAtPath:[[_gameURL URLByAppendingPathComponent:@"default.xex"] path]];
    if (!hasBase) {
        [self routeInstallationFlow];
        return;
    }
    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc]
        initForOpeningContentTypes:@[UTTypeData] asCopy:YES];
    picker.delegate = self;
    picker.allowsMultipleSelection = NO;
    [self presentViewController:picker animated:YES completion:nil];
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller
didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    if (controller == _saveImportPicker) {
        _saveImportPicker = nil;
        NSURL *selected = urls.firstObject;
        if (!selected) return;
        BOOL scoped = [selected startAccessingSecurityScopedResource];
        NSError *validationError = nil;
        BOOL hasSaves = NO, hasProfile = NO;
        BOOL valid = Theft4ValidateSaveExport(selected, &hasSaves, &hasProfile,
                                              &validationError);
        if (!valid) {
            if (scoped) [selected stopAccessingSecurityScopedResource];
            [self showSaveMessage:@"INVALID SAVE EXPORT"
                         detail:validationError.localizedDescription ?: @"Choose a Theft4-Saves folder."];
            return;
        }
        NSString *contents = hasSaves && hasProfile ? @"saved games and profile data" :
            (hasSaves ? @"saved games" : @"profile data");
        UIAlertController *confirm = [UIAlertController alertControllerWithTitle:@"IMPORT SAVES?"
            message:[NSString stringWithFormat:
                @"This will replace this app's %@ with the selected export. Existing data will be backed up to Files → Theft4 → Save Exports first. Keep the game closed during import.",
                contents] preferredStyle:UIAlertControllerStyleAlert];
        [confirm addAction:[UIAlertAction actionWithTitle:@"CANCEL"
            style:UIAlertActionStyleCancel handler:^(UIAlertAction *action) {
                (void)action;
                if (scoped) [selected stopAccessingSecurityScopedResource];
            }]];
        [confirm addAction:[UIAlertAction actionWithTitle:@"IMPORT"
            style:UIAlertActionStyleDestructive handler:^(UIAlertAction *action) {
                (void)action;
                self->_saveTransferBusy = YES;
                self->_importSavesButton.enabled = NO;
                self->_exportSavesButton.enabled = NO;
                NSURL *support = [self->_supportURL copy];
                NSURL *documents = [NSFileManager.defaultManager URLForDirectory:NSDocumentDirectory
                    inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:nil];
                dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
                    NSError *error = nil;
                    NSURL *backup = nil;
                    BOOL success = documents && Theft4InstallSaveExport(selected, support,
                                                                         documents, &backup, &error);
                    if (scoped) [selected stopAccessingSecurityScopedResource];
                    dispatch_async(dispatch_get_main_queue(), ^{
                        self->_importSavesButton.enabled = YES;
                        self->_exportSavesButton.enabled = YES;
                        self->_saveTransferBusy = NO;
                        [self record:success ? @"saves.import_completed" : @"saves.import_failed"];
                        NSString *detail = success ?
                            (backup ? [NSString stringWithFormat:
                                @"Save data is ready for the next game launch. Previous data was backed up as %@ in Files → Theft4 → Save Exports.",
                                backup.lastPathComponent] : @"Save data is ready for the next game launch.") :
                            (error.localizedDescription ?: @"The save data could not be imported.");
                        [self showSaveMessage:success ? @"SAVES IMPORTED" : @"SAVE IMPORT FAILED"
                                     detail:detail];
                    });
                });
            }]];
        [self presentViewController:confirm animated:YES completion:nil];
        return;
    }
    NSURL *selected = urls.firstObject;
    if (!selected || !_gameURL) {
        [self showInstallationTitle:@"TITLE UPDATE NOT INSTALLED"
                             detail:@"No title-update file was selected."
                        actionTitle:@"CHOOSE TITLE UPDATE" spinner:NO
                               step:Theft4InstallationStepUpdateFailed];
        return;
    }
    [self showInstallationTitle:@"INSTALLING TITLE UPDATE"
                         detail:@"Validating and extracting the selected Xbox 360 update…"
                    actionTitle:nil spinner:YES step:Theft4InstallationStepInstallingUpdate];
    const BOOL scoped = [selected startAccessingSecurityScopedResource];
    NSURL *game = _gameURL;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSError *fileError = nil;
        NSURL *sourceDirectory = [game URLByAppendingPathComponent:@".title-update-source"
                                                        isDirectory:YES];
        [NSFileManager.defaultManager removeItemAtURL:sourceDirectory error:nil];
        if (![NSFileManager.defaultManager createDirectoryAtURL:sourceDirectory
                                     withIntermediateDirectories:YES attributes:nil error:&fileError]) {
            if (scoped) [selected stopAccessingSecurityScopedResource];
            dispatch_async(dispatch_get_main_queue(), ^{
                [self showInstallationTitle:@"TITLE UPDATE NOT INSTALLED"
                                     detail:fileError.localizedDescription ?: @"Could not prepare title-update storage."
                                actionTitle:@"CHOOSE TITLE UPDATE" spinner:NO
                                       step:Theft4InstallationStepUpdateFailed];
            });
            return;
        }
        NSString *extension = selected.pathExtension;
        NSString *name = NSUUID.UUID.UUIDString;
        if (extension.length) name = [name stringByAppendingFormat:@".%@", extension];
        NSURL *stagedSource = [sourceDirectory URLByAppendingPathComponent:name];
        if (![NSFileManager.defaultManager copyItemAtURL:selected toURL:stagedSource error:&fileError]) {
            if (scoped) [selected stopAccessingSecurityScopedResource];
            [NSFileManager.defaultManager removeItemAtURL:sourceDirectory error:nil];
            dispatch_async(dispatch_get_main_queue(), ^{
                [self showInstallationTitle:@"TITLE UPDATE NOT INSTALLED"
                                     detail:fileError.localizedDescription ?: @"Could not import the selected file."
                                actionTitle:@"CHOOSE TITLE UPDATE" spinner:NO
                                       step:Theft4InstallationStepUpdateFailed];
            });
            return;
        }
        if (scoped) [selected stopAccessingSecurityScopedResource];

#ifdef THEFT4_HAS_GAME_LOADER
        char message[1024] = {};
        const int result = theft4_install_title_update(game.fileSystemRepresentation,
            stagedSource.fileSystemRepresentation, message, sizeof(message));
        NSString *detail = [NSString stringWithUTF8String:message];
#else
        const int result = 1;
        NSString *detail = @"This build does not include the title-update importer.";
#endif
        [NSFileManager.defaultManager removeItemAtURL:sourceDirectory error:nil];
        dispatch_async(dispatch_get_main_queue(), ^{
            if (result == 0) {
                self->_bootStatus = @"Title Update 8 installed. Ready to play.";
                self->_setupValidated = YES;
                [self showInstallationTitle:@"READY TO PLAY"
                                     detail:@"Theft4 validated and installed the matching title update."
                                actionTitle:@"OPEN MAIN SCREEN" spinner:NO
                                       step:Theft4InstallationStepReady];
                [self record:@"install.title_update_ready"];
                [self refresh];
            } else {
                [self showInstallationTitle:@"TITLE UPDATE NOT INSTALLED"
                                     detail:detail.length ? detail : @"The selected file is not a supported title update."
                                actionTitle:@"CHOOSE ANOTHER FILE" spinner:NO
                                       step:Theft4InstallationStepUpdateFailed];
                [self record:@"install.title_update_failed"];
            }
        });
    });
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller {
    if (controller == _saveImportPicker) {
        _saveImportPicker = nil;
        return;
    }
    if (_installationStep == Theft4InstallationStepSelectingUpdate) {
        [self showInstallationTitle:@"SELECT TITLE UPDATE"
                             detail:@"Choose the matching GTA IV Xbox 360 title-update file when you are ready."
                        actionTitle:@"SELECT TITLE UPDATE" spinner:NO
                               step:Theft4InstallationStepSelectingUpdate];
    }
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    const CGFloat scale = self.view.window.screen.scale ?: UIScreen.mainScreen.scale;
    theft4_metal_resize_layer((__bridge void *)_metalView.layer,
                              _metalView.bounds.size.width,
                              _metalView.bounds.size.height, scale);
}

- (void)displaySettingsChanged:(UIControl *)sender {
    [self syncA19OutputChoice];
    if (sender == _fsrBoost && _fsrBoost.on) _enhancedOutput.on = YES;
    if (sender == _enhancedOutput && !_enhancedOutput.on) _fsrBoost.on = NO;
    [NSUserDefaults.standardUserDefaults setBool:_performanceCapture.on
                                          forKey:@"Theft4DetailedPerformanceCapture"];
    [NSUserDefaults.standardUserDefaults setBool:_fsrBoost.on forKey:@"Theft4ExperimentalFSRBoost"];
    [NSUserDefaults.standardUserDefaults setBool:_motionBlur.on forKey:@"Theft4MotionBlur"];
    NSArray<UISegmentedControl *> *choices = @[_shadowQuality, _drawDistance, _modelDetail,
        _reflectionQuality, _antiAliasing];
    NSArray<NSString *> *keys = @[@"Theft4ShadowQuality", @"Theft4DrawDistance",
        @"Theft4ModelDetail", @"Theft4ReflectionQuality", @"Theft4AntiAliasing"];
    for (NSUInteger i = 0; i < choices.count; ++i)
        [NSUserDefaults.standardUserDefaults setInteger:choices[i].selectedSegmentIndex forKey:keys[i]];
    if (_bringupOverlay.renderResolution) {
        [NSUserDefaults.standardUserDefaults setInteger:_bringupOverlay.renderHeight forKey:@"Theft4LabRenderHeight"];
        [NSUserDefaults.standardUserDefaults setBool:_bringupOverlay.fsrUpscaling.on forKey:@"Theft4LabFSREnabled"];
    }
    [_bringupOverlay refreshConfigurationSummary];
    [NSUserDefaults.standardUserDefaults setBool:_showFPS.on forKey:@"Theft4ShowFPS"];
    [NSUserDefaults.standardUserDefaults setBool:_showControls.on forKey:@"Theft4ShowTouchControls"];
    [NSUserDefaults.standardUserDefaults setBool:_anisotropicFiltering.on
        forKey:@"Theft4AnisotropicFiltering"];
    [NSUserDefaults.standardUserDefaults setBool:_enhancedOutput.on
        forKey:@"Theft4EnhancedOutput1080p"];
    _fpsLabel.hidden = !_gamePresentation || !_showFPS.on;
    [NSUserDefaults.standardUserDefaults setBool:_showFrameTime.on forKey:@"Theft4ShowFrameTime"];
    [self updateFrameTimeHUD];
    _touchControls.active = _gamePresentation && _showControls.on;
    _fpsLastFrames = theft4_frame_counter_published_frames();
    _fpsLastTime = CACurrentMediaTime();
}

- (void)syncA19OutputChoice {
    if (!_bringupOverlay.renderResolution ||
        strcmp(getenv("THEFT4_DEVICE_PROFILE") ?: "", "a19") != 0) return;
    _bringupOverlay.fsrUpscaling.on = _bringupOverlay.renderHeight < 1080;
    _bringupOverlay.fsrUpscaling.enabled = NO;
}

- (void)applyLowPowerPreset {
    if (!_bringupOverlay.renderResolution || _executionAttempted) return;
    _bringupOverlay.renderResolution.selectedSegmentIndex = 0;
    _bringupOverlay.fsrUpscaling.on = YES;
    for (UISegmentedControl *choice in @[_shadowQuality, _drawDistance, _modelDetail,
                                          _reflectionQuality, _antiAliasing])
        choice.selectedSegmentIndex = 0;
    _anisotropicFiltering.on = NO;
    _motionBlur.on = NO;
    [self displaySettingsChanged:_bringupOverlay.renderResolution];
}

- (void)record:(NSString *)event {
    static os_log_t log;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ log = os_log_create(NSBundle.mainBundle.bundleIdentifier.UTF8String, "lifecycle"); });
    NSMutableDictionary *entry = [@{@"event":event, @"time":@([NSDate timeIntervalSinceReferenceDate]),
        @"pid":@(NSProcessInfo.processInfo.processIdentifier)} mutableCopy];
    theft4_core_snapshot snapshot = {.struct_size = sizeof(snapshot), .abi_version = THEFT4_CORE_ABI_VERSION};
    if (_core && theft4_core_get_snapshot(_core, &snapshot) == THEFT4_OK) {
        entry[@"state"] = @(snapshot.state);
        entry[@"page_bytes"] = @(snapshot.host_page_bytes);
        entry[@"platform"] = @(snapshot.platform);
        entry[@"abi"] = @(snapshot.abi_version);
        entry[@"core_version"] = @(snapshot.core_version);
        entry[@"guest_runtime_initialized"] = @(snapshot.guest_runtime_initialized);
    }
    NSData *data = [NSJSONSerialization dataWithJSONObject:entry options:NSJSONWritingSortedKeys error:nil];
    NSString *line = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    os_log_with_type(log, OS_LOG_TYPE_INFO, "%{public}@", line);
    if (_logURL && data) {
        // Bound bring-up diagnostics; never touch game/save files. NSLog is not
        // the sole evidence channel because device console capture can detach.
        NSDictionary *attrs = [NSFileManager.defaultManager attributesOfItemAtPath:_logURL.path error:nil];
        const char *mode = [attrs fileSize] > 262144 ? "w" : "a";
        FILE *file = fopen(_logURL.fileSystemRepresentation, mode);
        if (file) { fwrite(data.bytes, 1, data.length, file); fputc('\n', file); fclose(file); }
        else { _failure = @"Cannot write lifecycle diagnostics"; }
    }
}

- (void)downloadLatestLogCapture {
    if (!_supportURL || !_downloadLogButton.enabled) return;
    [self record:@"diagnostics.export_requested"];
    _downloadLogButton.enabled = NO;
    NSURL *supportURL = [_supportURL copy];
    NSURL *lifecycleURL = [_logURL copy];
    BOOL performanceCapture = _performanceCapture.on;
    NSString *version = NSBundle.mainBundle.infoDictionary[@"CFBundleShortVersionString"] ?: @"unknown";
    NSString *build = NSBundle.mainBundle.infoDictionary[@"CFBundleVersion"] ?: @"unknown";
    NSString *systemVersion = UIDevice.currentDevice.systemVersion ?: @"unknown";
    NSString *deviceName = UIDevice.currentDevice.model ?: @"unknown";
    uint64_t physicalMemory = NSProcessInfo.processInfo.physicalMemory;
    const char *machineCString = getenv("THEFT4_DEVICE_MODEL");
    NSString *machine = machineCString ? [NSString stringWithUTF8String:machineCString] : nil;
    if (!machine.length) {
        struct utsname systemInfo = {};
        machine = uname(&systemInfo) == 0 ? [NSString stringWithUTF8String:systemInfo.machine] : @"unknown";
    }
    NSString *profile = [NSString stringWithUTF8String:getenv("THEFT4_DEVICE_PROFILE") ?: "unknown"];
    BOOL showFPS = _showFPS.on;
    BOOL anisotropy = _anisotropicFiltering.on;
    BOOL enhancedOutput = _enhancedOutput.on;
    BOOL fsrBoost = _fsrBoost.on;
    BOOL motionBlur = _motionBlur.on;

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        NSFileManager *fm = NSFileManager.defaultManager;
        NSError *error = nil;
        NSURL *documents = [fm URLForDirectory:NSDocumentDirectory inDomain:NSUserDomainMask
                             appropriateForURL:nil create:YES error:&error];
        NSURL *diagnosticsURL = [documents URLByAppendingPathComponent:@"Diagnostics"
                                                              isDirectory:YES];
        if (!documents || ![fm createDirectoryAtURL:diagnosticsURL withIntermediateDirectories:YES
                                         attributes:nil error:&error]) {
            dispatch_async(dispatch_get_main_queue(), ^{
                self->_downloadLogButton.enabled = YES;
                UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"LOG EXPORT FAILED"
                    message:error.localizedDescription ?: @"The Files folder is unavailable."
                    preferredStyle:UIAlertControllerStyleAlert];
                [alert addAction:[UIAlertAction actionWithTitle:@"OK"
                    style:UIAlertActionStyleDefault handler:nil]];
                [self presentViewController:alert animated:YES completion:nil];
            });
            return;
        }

        NSDateFormatter *formatter = [NSDateFormatter new];
        formatter.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
        formatter.dateFormat = @"yyyy-MM-dd-HH-mm-ss";
        NSString *stamp = [formatter stringFromDate:[NSDate date]];
        NSURL *outputURL = [diagnosticsURL
            URLByAppendingPathComponent:[NSString stringWithFormat:@"Theft4-Performance-Capture-%@.txt",
                                         stamp]];
        NSOutputStream *stream = [NSOutputStream outputStreamWithURL:outputURL append:NO];
        [stream open];
        NSUInteger budget = 50u * 1024u * 1024u;
        NSMutableArray<NSString *> *included = [NSMutableArray new];
        NSMutableArray<NSString *> *skipped = [NSMutableArray new];
        NSString *metadata = [NSString stringWithFormat:
            @"Theft4 performance capture export\n"
             "Generated: %@\n"
             "App: %@ %@ (%@)\n"
             "Bundle: %@\n"
             "Device: %@ / %@\n"
             "iOS/iPadOS: %@\n"
             "Physical memory: %llu MiB\n"
             "Theft4 device profile: %@\n"
             "Detailed capture switch at export: %@\n"
             "Frame counter enabled: %@\n"
             "Anisotropic filtering: %@\n"
             "Enhanced 1080p output: %@\n"
             "FSR boost: %@\n"
             "Motion blur: %@\n"
             "This is a bounded text bundle. Native performance CSV/JSON artifacts are included below when available.\n",
            [NSDate date], Theft4DisplayName(), version, build,
            NSBundle.mainBundle.bundleIdentifier ?: @"unknown", deviceName, machine,
            systemVersion, (unsigned long long)(physicalMemory >> 20), profile,
            performanceCapture ? @"ON" : @"OFF", showFPS ? @"ON" : @"OFF",
            anisotropy ? @"ON" : @"OFF", enhancedOutput ? @"ON" : @"OFF",
            fsrBoost ? @"ON" : @"OFF", motionBlur ? @"ON" : @"OFF"];
        BOOL success = Theft4WriteString(stream, metadata, &budget);

        NSURL *applicationSupport = [supportURL URLByDeletingLastPathComponent];
        NSURL *startupURL = [supportURL URLByAppendingPathComponent:@"startup" isDirectory:YES];
        NSURL *nativeDiagnosticsURL = [[applicationSupport
            URLByAppendingPathComponent:@"LibertyRecomp" isDirectory:YES]
            URLByAppendingPathComponent:@"Diagnostics" isDirectory:YES];
        success = success && Theft4AppendDiagnosticFile(stream, lifecycleURL,
            @"Theft4/lifecycle.jsonl", &budget, included, skipped);
        success = success && Theft4AppendDiagnosticFile(stream,
            [supportURL URLByAppendingPathComponent:@"runtime.log"],
            @"Theft4/runtime.log", &budget, included, skipped);
        success = success && Theft4AppendDiagnosticFile(stream,
            [startupURL URLByAppendingPathComponent:@"runtime.log"],
            @"Theft4/startup/runtime.log", &budget, included, skipped);

        NSDirectoryEnumerator *nativeEnumerator =
            [fm enumeratorAtURL:nativeDiagnosticsURL
     includingPropertiesForKeys:@[NSURLIsRegularFileKey]
                        options:0 errorHandler:^BOOL(NSURL *url, NSError *enumerationError) {
                            [skipped addObject:[NSString stringWithFormat:@"%@ (%@)",
                                url.lastPathComponent, enumerationError.localizedDescription]];
                            return YES;
                        }];
        for (NSURL *url in nativeEnumerator) {
            NSNumber *regular = nil;
            [url getResourceValue:&regular forKey:NSURLIsRegularFileKey error:nil];
            if (!regular.boolValue || !Theft4DiagnosticTextExtension(url.pathExtension)) continue;
            NSString *label = [NSString stringWithFormat:@"LibertyRecomp/Diagnostics/%@",
                               [url.path substringFromIndex:nativeDiagnosticsURL.path.length + 1]];
            success = success && Theft4AppendDiagnosticFile(stream, url, label,
                                                              &budget, included, skipped);
            if (!success) break;
        }

        for (NSString *directoryName in @[@"audio-timing", @"frame-captures"]) {
            NSURL *directoryURL = [startupURL URLByAppendingPathComponent:directoryName
                                                               isDirectory:YES];
            NSDirectoryEnumerator *enumerator =
                [fm enumeratorAtURL:directoryURL includingPropertiesForKeys:@[NSURLIsRegularFileKey]
                            options:0 errorHandler:^BOOL(NSURL *url, NSError *enumerationError) {
                                [skipped addObject:[NSString stringWithFormat:@"%@ (%@)",
                                    url.lastPathComponent, enumerationError.localizedDescription]];
                                return YES;
                            }];
            for (NSURL *url in enumerator) {
                NSNumber *regular = nil;
                [url getResourceValue:&regular forKey:NSURLIsRegularFileKey error:nil];
                if (!regular.boolValue || !Theft4DiagnosticTextExtension(url.pathExtension)) continue;
                NSString *label = [NSString stringWithFormat:@"Theft4/startup/%@/%@",
                                   directoryName,
                                   [url.path substringFromIndex:directoryURL.path.length + 1]];
                success = success && Theft4AppendDiagnosticFile(stream, url, label,
                                                                  &budget, included, skipped);
                if (!success) break;
            }
            if (!success) break;
        }

        if (success) {
            NSString *manifest = [NSString stringWithFormat:
                @"\n\n===== EXPORT MANIFEST =====\nIncluded files (%lu):\n%@\n"
                 "Skipped files (%lu):\n%@\nRemaining export budget: %lu bytes\n",
                (unsigned long)included.count, [included componentsJoinedByString:@"\n"],
                (unsigned long)skipped.count, [skipped componentsJoinedByString:@"\n"],
                (unsigned long)budget];
            success = Theft4WriteString(stream, manifest, &budget);
        }
        [stream close];

        if (!success) {
            [fm removeItemAtURL:outputURL error:nil];
            dispatch_async(dispatch_get_main_queue(), ^{
                self->_downloadLogButton.enabled = YES;
                UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"LOG EXPORT FAILED"
                    message:@"The diagnostic bundle could not be written."
                    preferredStyle:UIAlertControllerStyleAlert];
                [alert addAction:[UIAlertAction actionWithTitle:@"OK"
                    style:UIAlertActionStyleDefault handler:nil]];
                [self presentViewController:alert animated:YES completion:nil];
            });
            return;
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            self->_downloadLogButton.enabled = YES;
            [self record:@"diagnostics.export_completed"];
            UIActivityViewController *share =
                [[UIActivityViewController alloc] initWithActivityItems:@[outputURL]
                                                    applicationActivities:nil];
            UIPopoverPresentationController *popover = share.popoverPresentationController;
            popover.sourceView = self->_downloadLogButton;
            popover.sourceRect = self->_downloadLogButton.bounds;
            [self presentViewController:share animated:YES completion:nil];
        });
    });
}

- (void)refresh {
    theft4_core_snapshot snapshot = {.struct_size = sizeof(snapshot), .abi_version = THEFT4_CORE_ABI_VERSION};
    if (_core && theft4_core_get_snapshot(_core, &snapshot) == THEFT4_OK) {
        NSArray *states = @[@"Core ready", @"Core active", @"Core paused", @"Core stopped"];
        _status.text = _failure ?: (_bootStatus ?: states[snapshot.state]);
        _detail.text = [NSString stringWithFormat:@"Platform  %s\nCore      %s\nC ABI     %u\nPage size %llu bytes\nGame progress is reported above.\n\nLogs: System → Download Latest Log Capture",
            snapshot.platform, snapshot.core_version, snapshot.abi_version, (unsigned long long)snapshot.host_page_bytes];
    } else {
        _status.text = _failure ?: @"Core stopped";
        _detail.text = @"No core session is active.";
    }
}

- (void)bootEvent:(NSString *)event {
    _bootStatus = event;
    [self record:[@"boot." stringByAppendingString:event]];
    [self refresh];
    if ([event isEqualToString:@"Recompiled GTA IV entry point is executing"]) {
        [self enterGamePresentationMode];
    }
}

- (void)enterGamePresentationMode {
    if (_gamePresentation) return;
    [_bringupOverlay retireScene];
    _gamePresentation = YES;
    self.controllerUserInteractionEnabled = NO;
    [UIView animateWithDuration:0.2 animations:^{
        self->_bringupOverlay.alpha = 0.0;
    } completion:^(BOOL finished) {
        (void)finished;
        self->_bringupOverlay.hidden = YES;
        self->_bringupOverlay.userInteractionEnabled = NO;
    }];
    [self setNeedsStatusBarAppearanceUpdate];
    [self setNeedsUpdateOfHomeIndicatorAutoHidden];
    _fpsLastFrames = theft4_frame_counter_published_frames();
    _fpsLastTime = CACurrentMediaTime();
    _fpsLabel.text = @"--.- FPS";
    _fpsLabel.hidden = !_showFPS.on;
    _touchControls.active = _showControls.on &&
        self.view.window.windowScene.activationState == UISceneActivationStateForegroundActive;
    [self updateFrameTimeHUD];
    [self record:@"ui.game_presentation_mode"];
}

- (void)updateFrameTimeHUD {
    BOOL visible = _gamePresentation && _sceneActive && _showFrameTime.on;
    _frameTimeView.hidden = !visible;
    _frameTimeTop.constant = _showFPS.on ? 52 : 10;
    theft4_frame_time_set_enabled(visible);
    if (!visible) {
        [_frameTimeTimer invalidate];
        _frameTimeTimer = nil;
        theft4_frame_time_snapshot empty = {0};
        [_frameTimeView updateWithSnapshot:&empty];
    } else if (!_frameTimeTimer) {
        __weak Theft4ViewController *weakSelf = self;
        _frameTimeTimer = [NSTimer timerWithTimeInterval:0.1 repeats:YES block:^(NSTimer *timer) {
            Theft4ViewController *controller = weakSelf;
            if (!controller) { [timer invalidate]; return; }
            theft4_frame_time_snapshot snapshot = {0};
            theft4_frame_time_copy(&snapshot);
            [controller->_frameTimeView updateWithSnapshot:&snapshot];
            const int status = rex_gta4_native_profile_status();
            if (controller->_nativeProfileRequested && !controller->_nativeProfileFinished &&
                (status == 2 || status == -1)) {
                controller->_nativeProfileFinished = YES;
                [controller->_frameTimeView setCaptureCompleted:(status == 2)];
                [controller record:(status == 2 ? @"capture.complete" : @"capture.failed")];
            }
        }];
        [NSRunLoop.mainRunLoop addTimer:_frameTimeTimer forMode:NSRunLoopCommonModes];
    }
}

- (void)requestNativeProfile {
    if (!_gamePresentation || _nativeProfileRequested) return;
    if (rex_gta4_native_profile_start()) {
        _nativeProfileRequested = YES;
        _nativeProfileFinished = NO;
        [_frameTimeView setCaptureRequested:YES];
        [self record:@"capture.requested"];
    }
}

- (void)refreshFrameRate {
    if (_fpsLabel.hidden) return;
    const uint64_t frames = theft4_frame_counter_published_frames();
    const CFTimeInterval now = CACurrentMediaTime();
    const CFTimeInterval elapsed = now - _fpsLastTime;
    if (elapsed >= 0.2) {
        const double fps = (double)(frames - _fpsLastFrames) / elapsed;
        _fpsLabel.text = [NSString stringWithFormat:@"%4.1f FPS", fps];
        _fpsLastFrames = frames;
        _fpsLastTime = now;
    }
}

- (BOOL)prefersStatusBarHidden { return _executionAttempted; }
- (BOOL)prefersHomeIndicatorAutoHidden { return _executionAttempted; }

- (void)prepareTransferredGame {
    [self startGamePreparation:_gameURL];
}

- (void)startGamePreparation:(NSURL *)game {
    [self startGamePreparation:game execute:NO];
}

- (void)startTransferredGame {
    [self startGamePreparation:_gameURL execute:YES];
}

- (void)startGamePreparation:(NSURL *)game execute:(BOOL)execute {
#ifdef THEFT4_HAS_GAME_LOADER
    if (_loading || _executionAttempted || _saveTransferBusy || !game || !_supportURL || _failure) return;
#ifndef THEFT4_HAS_GAME_STARTUP
    if (execute) return;
#endif
    BOOL directory = NO;
    if (![NSFileManager.defaultManager fileExistsAtPath:game.path isDirectory:&directory] || !directory) {
        [self bootEvent:@"Copy the extracted base game into Theft4, then select its title update."];
        return;
    }
    // The native profiler is armed before the one-shot runtime is created.
    // Its bounded files survive process termination and are exported from the
    // System tab on the next launch.
    setenv("THEFT4_PERFORMANCE_CAPTURE", _performanceCapture.on ? "1" : "0", 1);
    if (theft4_configure_boot_diagnostics() != 0) {
        [self bootEvent:@"Cannot configure loader diagnostics"];
        return;
    }
    if (execute) {
        // Apply the persisted launcher choice before the background runtime
        // reads and validates its native-renderer launch configuration.
        setenv("THEFT4_ANISOTROPY", _anisotropicFiltering.on ? "4x" : "1x", 1);
        setenv("THEFT4_MOTION_BLUR", _motionBlur.on ? "1" : "0", 1);
        const char *shadowPresets[] = {"original", "enhanced", "ultra"};
        const char *distancePresets[] = {"1", "2", "3"};
        const char *reflectionPresets[] = {"original", "1080p", "full"};
        const char *antiAliasingPresets[] = {"off", "fxaa", "smaa"};
        setenv("THEFT4_SHADOW_QUALITY", shadowPresets[_shadowQuality.selectedSegmentIndex], 1);
        setenv("THEFT4_DRAW_DISTANCE", distancePresets[_drawDistance.selectedSegmentIndex], 1);
        setenv("THEFT4_FORCE_HIGHEST_LOD", _modelDetail.selectedSegmentIndex ? "1" : "0", 1);
        setenv("THEFT4_REFLECTION_RESOLUTION",
            reflectionPresets[_reflectionQuality.selectedSegmentIndex], 1);
        setenv("THEFT4_ANTI_ALIASING", antiAliasingPresets[_antiAliasing.selectedSegmentIndex], 1);
        [self.view layoutIfNeeded];
        UIScreen *screen = self.view.window.screen ?: UIScreen.mainScreen;
        CGFloat nativeScale = screen.nativeScale;
        const uint32_t nativeWidth = (uint32_t)floor(_metalView.bounds.size.width * nativeScale);
        const uint32_t nativeHeight = (uint32_t)floor(_metalView.bounds.size.height * nativeScale);
        if (_bringupOverlay.renderResolution) {
            theft4_metal_set_lab_output(_bringupOverlay.renderHeight,
                _bringupOverlay.fsrUpscaling.on, nativeWidth, nativeHeight,
                strcmp(getenv("THEFT4_DEVICE_PROFILE") ?: "", "a19") == 0);
        } else {
            theft4_metal_set_output_mode(
                _fsrBoost.on ? THEFT4_OUTPUT_FSR_BOOST :
                    (_enhancedOutput.on ? THEFT4_OUTPUT_FSR_1080P : THEFT4_OUTPUT_720P),
                nativeWidth, nativeHeight);
        }
        // Release all decorative GPU work before initializing the game device.
        [_bringupOverlay retireScene];
        _fsrBoost.enabled = NO;
        _bringupOverlay.renderResolution.enabled = NO;
        _bringupOverlay.fsrUpscaling.enabled = NO;
        _bringupOverlay.restartButton.enabled = NO;
        _enhancedOutput.enabled = NO;
        _anisotropicFiltering.enabled = NO;
        _motionBlur.enabled = NO;
        for (UISegmentedControl *choice in @[_shadowQuality, _drawDistance, _modelDetail,
                                              _reflectionQuality, _antiAliasing])
            choice.enabled = NO;
    }
    if (![self isSetupComplete]) {
        NSError *backupError = nil;
        if (![game setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:&backupError])
            [self record:@"boot.cannot_exclude_game_from_backup"];
    }
    _loading = YES;
    _prepare.enabled = NO;
    _start.enabled = NO;
    if (execute) _executionAttempted = YES;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        // Retain the controller through completion; all UIKit/core callbacks
        // are delivered back on the main queue. Saves never go in Documents/game.
        int result;
#ifdef THEFT4_HAS_GAME_STARTUP
        if (execute) {
            result = theft4_start_game(game.fileSystemRepresentation, self->_supportURL.fileSystemRepresentation,
                                      bootEvent, (__bridge void *)self);
        } else
#endif
        result = theft4_prepare_game(game.fileSystemRepresentation, self->_supportURL.fileSystemRepresentation,
                                        bootEvent, (__bridge void *)self);
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_loading = NO;
            self->_prepare.enabled = !self->_executionAttempted;
            self->_start.enabled = !self->_executionAttempted;
            [self record:execute ? @"boot.execution_attempt_returned" :
                (result == 0 ? @"boot.preparation_completed" : @"boot.preparation_failed")];
        });
    });
#else
    [self bootEvent:@"This build does not include the game loader."];
#endif
}

- (BOOL)accept:(theft4_result)result operation:(NSString *)operation {
    if (result == THEFT4_OK) return YES;
    _failure = [NSString stringWithFormat:@"%@ failed (%d)", operation, result];
    [self record:[@"error." stringByAppendingString:operation]];
    [self refresh];
    return NO;
}

- (void)createCore {
    if (_core || _failure) { [self refresh]; return; }
    theft4_core_config config = {sizeof(config), THEFT4_CORE_ABI_VERSION,
        NSBundle.mainBundle.resourcePath.UTF8String, _supportURL.path.UTF8String,
        coreEvent, (__bridge void *)self};
    [self accept:theft4_core_create(&config, &_core) operation:@"create"];
    [self refresh];
}

- (void)activate {
    [_bringupOverlay setActive:!_executionAttempted];
    _sceneActive = YES;
    [self updateFrameTimeHUD];
    _touchControls.active = _gamePresentation && _showControls.on;
    [self createCore];
    if (_core) [self accept:theft4_core_activate(_core) operation:@"activate"];
    [self record:@"scene.active"];
#ifdef THEFT4_HAS_GAME_STARTUP
    if (!_bootRan && [NSProcessInfo.processInfo.arguments containsObject:@"--theft4-start-game"]) {
        _bootRan = YES;
        [self startTransferredGame];
    }
#endif
#ifdef THEFT4_HAS_GAME_LOADER
    if (!_bootRan && [NSProcessInfo.processInfo.arguments containsObject:@"--theft4-prepare-game"]) {
        _bootRan = YES;
        NSURL *game = [_supportURL URLByAppendingPathComponent:@"game" isDirectory:YES];
        [self startGamePreparation:game];
    }
#endif
    // Optional bring-up-only teardown exercise; not a substitute for real
    // background/foreground events. It never runs guest or graphics work.
    if (!_selfTestRan && [NSProcessInfo.processInfo.arguments containsObject:@"--theft4-self-test"]) {
        _selfTestRan = YES;
        for (unsigned i = 0; i < 3 && !_failure; ++i) {
            [self pause];
            [self shutdown];
            [self createCore];
            if (_core) [self accept:theft4_core_activate(_core) operation:@"activate"];
        }
        [self record:_failure ? @"selftest.failed" : @"selftest.completed"];
    }
    [self refresh];
}
- (void)pause {
    [_bringupOverlay setActive:NO];
    _sceneActive = NO;
    [self updateFrameTimeHUD];
    _touchControls.active = NO;
    if (_core) [self accept:theft4_core_pause(_core) operation:@"pause"];
    [self refresh];
}
- (void)shutdown {
    [_bringupOverlay setActive:NO];
    _sceneActive = NO;
    [self updateFrameTimeHUD];
    _touchControls.active = NO;
    if (_core) {
        [self accept:theft4_core_stop(_core) operation:@"stop"];
        [self accept:theft4_core_destroy(_core) operation:@"destroy"];
        _core = NULL;
    }
    [self refresh];
}
- (void)restartCore {
    [self shutdown];
    _failure = nil;
    [self createCore];
    if (self.view.window.windowScene.activationState == UISceneActivationStateForegroundActive) [self activate];
}
- (void)didReceiveMemoryWarning {
    [super didReceiveMemoryWarning];
    if (_core) [self accept:theft4_core_memory_warning(_core) operation:@"memory_warning"];
    [self refresh];
}
- (void)dealloc {
    // Scene disconnect normally releases it first. No callback may access a
    // partially deallocated controller; scene ownership requires shutdown.
    NSCAssert(_core == NULL, @"Scene must shut down its core before release");
    [_fpsTimer invalidate];
    theft4_metal_unbind_layer((__bridge void *)_metalView.layer);
}
@end

@interface Theft4SceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow *window;
@property(nonatomic, strong) Theft4ViewController *controller;
@end
@implementation Theft4SceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    if (![scene isKindOfClass:UIWindowScene.class]) return;
    self.controller = [Theft4ViewController new];
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    self.window.rootViewController = self.controller;
    [self.window makeKeyAndVisible];
}
- (void)sceneDidBecomeActive:(UIScene *)scene { [self.controller activate]; }
- (void)sceneWillResignActive:(UIScene *)scene { [self.controller pause]; }
- (void)sceneDidEnterBackground:(UIScene *)scene { [self.controller record:@"scene.background"]; }
- (void)sceneWillEnterForeground:(UIScene *)scene { [self.controller record:@"scene.foreground"]; }
- (void)sceneDidDisconnect:(UIScene *)scene {
    [self.controller record:@"scene.disconnected"];
    [self.controller shutdown];
    self.window = nil;
    self.controller = nil;
}
@end

@interface Theft4AppDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation Theft4AppDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options { return YES; }
@end

int main(int argc, char *argv[]) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(Theft4AppDelegate.class)); }
}
