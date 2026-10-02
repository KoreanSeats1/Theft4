#import <UIKit/UIKit.h>
#import <GameController/GameController.h>
#import <Metal/MTLDeviceCertification.h>
#import <QuartzCore/CAMetalLayer.h>
#import <os/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <stdint.h>
#include <mach-o/dyld.h>
#include "../../glue/rexglue-sdk-main/include/rex/diagnostics/runtime_callers.h"
#include "../../glue/rexglue-sdk-main/include/rex/diagnostics/frame_scheduling.h"
#include "theft4_core.h"
#include "theft4_device_profile.h"
#include "theft4_metal_presenter.h"
#include "../../glue/rexglue-sdk-main/src/graphics/gta4_native/native_light_capture.h"
#include "../../glue/rexglue-sdk-main/include/rex/fault_diagnostics.h"
#include "theft4_lab_diagnostics.h"
#import "Theft4FrameTimeView.h"
#import "Theft4CPUUsageView.h"
extern int rex_gta4_native_profile_start(void);
extern int rex_gta4_native_profile_status(void);
#import "Theft4TouchControls.h"
#import "Theft4LauncherView.h"
#ifdef THEFT4_HAS_GAME_LOADER
#include "theft4_boot.h"
#endif

static NSString *Theft4PerformanceProfileFields(void) {
    NSProcessInfo *process = NSProcessInfo.processInfo;
    BOOL standard = [process hasPerformanceProfile:NSProcessPerformanceProfileDefault];
    BOOL sustained = [process hasPerformanceProfile:NSProcessPerformanceProfileSustained];
    // Two false answers mean unknown, not proof of the default profile.
    NSString *profile = standard && sustained ? @"both" : sustained ? @"sustained" :
        standard ? @"default" : @"unknown";
    return [NSString stringWithFormat:@"performance_profile=%@ profile_default=%d profile_sustained=%d",
        profile, standard, sustained];
}

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
    if ([included containsObject:label]) return YES;
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
    UISwitch *_showCPUUsage;
    Theft4CPUUsageView *_cpuUsageView;
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
    UISwitch *_depthOfField;
    UISegmentedControl *_shadowQuality;
    UISegmentedControl *_drawDistance;
    UISegmentedControl *_modelDetail;
    UISegmentedControl *_reflectionQuality;
    UISegmentedControl *_antiAliasing;
    UISwitch *_performanceCapture;
    UISwitch *_runtimeWaitImprovements;
    UISwitch *_directGuestClock;
    UISwitch *_frameScheduling;
    UISwitch *_prewarmTargetReuse;
    UISwitch *_rendererEfficiency;
    UISwitch *_parallelPreparation;
    UISwitch *_frameAssembly;
    UISwitch *_graphicsPreparation;
    UISwitch *_fusedSmaa;
    UISwitch *_hardwareSmaa;
    UISwitch *_frameResourceSharing;
    UISwitch *_parallelTextureConversion;
    UISwitch *_memoryRecovery;
    UISwitch *_commandStream;
    UISwitch *_cpuCleanup;
    NSURL *_schedulingURL;
    NSMutableString *_schedulingRows; // serial export queue owns capture storage
    NSUInteger _schedulingBytes;
    BOOL _schedulingFull;
    dispatch_source_t _runtimeCallerTimer;
    NSURL *_runtimeCallerURL;
    NSMutableString *_runtimeCallerRows; // export queue only after start
    NSUInteger _runtimeCallerBytes; // export queue only, bounded across flushes
    BOOL _runtimeCallerFull; // export queue only
    BOOL _publicationCaptureActive;
    BOOL _publicationCaptureWriteFailed;
    NSURL *_publicationCaptureURL;
    NSURL *_lightCaptureURL;
    uint64_t _lightCaptureCursor;
    dispatch_queue_t _publicationCaptureQueue;
    uint64_t _publicationCaptureCursor;
    CFTimeInterval _publicationCaptureStartTime;
    NSUInteger _publicationCaptureBytes; // export queue only
    UIButton *_downloadLogButton;
    Theft4TouchControls *_touchControls;
    BOOL _gamePresentation;
    BOOL _legacyIPadProfile;
    BOOL _limitedMemoryProfile;
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
- (void)updateFrameTimeHUD;
- (BOOL)beginPublicationCapture;
- (void)drainPublicationCapture;
- (void)collectRuntimeCallers;
- (void)collectFrameScheduling;
- (void)flushFrameScheduling;
- (void)setFrameSchedulingEnabled:(BOOL)enabled;
- (void)flushRuntimeCallers;
- (void)appendPublicationCaptureText:(NSString *)text;
- (void)stopPublicationCapture;
- (void)markPerformanceScene:(UILongPressGestureRecognizer *)gesture;
- (void)applyLowPowerPreset;
- (void)applyLimitedMemoryCaps;
- (void)applyOriginalGraphicsPreset;
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

typedef NS_ENUM(NSInteger, Theft4AutomaticGraphicsTier) {
    Theft4AutomaticGraphicsTierOriginal,
    Theft4AutomaticGraphicsTierLow,
    Theft4AutomaticGraphicsTierIPhone17Pro,
    Theft4AutomaticGraphicsTierM5IPad
};

static Theft4AutomaticGraphicsTier automaticGraphicsTier(void) {
    const char *machine = getenv("THEFT4_DEVICE_MODEL");
    struct utsname systemInfo = {};
    if (!machine && uname(&systemInfo) == 0) machine = systemInfo.machine;
    if (!machine) return Theft4AutomaticGraphicsTierOriginal;
    unsigned major = 0, minor = 0;
    if (sscanf(machine, "iPad%u,%u", &major, &minor) == 2)
        return major == 17 && minor >= 1 && minor <= 4
            ? Theft4AutomaticGraphicsTierM5IPad : Theft4AutomaticGraphicsTierOriginal;
    if (sscanf(machine, "iPhone%u,%u", &major, &minor) != 2)
        return Theft4AutomaticGraphicsTierOriginal;
    if (major <= 16 || (major == 18 && minor == 4))
        return Theft4AutomaticGraphicsTierLow;
    if (major == 18 && (minor == 1 || minor == 2))
        return Theft4AutomaticGraphicsTierIPhone17Pro;
    // iPhone17,* is the 16 family; iPhone18,3/5 are the standard 17 family.
    return Theft4AutomaticGraphicsTierOriginal;
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
    [NSNotificationCenter.defaultCenter addObserver:self
        selector:@selector(performanceProfileChanged:)
        name:NSProcessInfoPerformanceProfileDidChangeNotification object:nil];
    self.controllerUserInteractionEnabled = YES;
    _legacyIPadProfile = configureDeviceProfile();
    const char *deviceProfile = getenv("THEFT4_DEVICE_PROFILE") ?: "";
    _limitedMemoryProfile = strcmp(deviceProfile, "iphone-6gb") == 0 || strcmp(deviceProfile, "legacy-ipad") == 0;
    [NSUserDefaults.standardUserDefaults registerDefaults:@{
        @"Theft4ShowCPUUsage": @YES,
        @"Theft4ShowFPS": @YES,
        @"Theft4ShowFrameTime": @NO,
        @"Theft4ShowTouchControls": @NO,
        @"Theft4AnisotropicFiltering": @(!_legacyIPadProfile),
        @"Theft4EnhancedOutput1080p": @(!_legacyIPadProfile),
        @"Theft4ExperimentalFSRBoost": @NO,
        @"Theft4MotionBlur": @NO,
        @"Theft4DepthOfField": @YES,
        @"Theft4ShadowQuality": @1,
        @"Theft4DrawDistance": @1,
        @"Theft4ModelDetail": @1,
        @"Theft4ReflectionQuality": @0,
        @"Theft4AntiAliasing": @2,
        @"Theft4RuntimeWaitImprovements": @YES,
        @"Theft4DirectGuestClock": @YES,
        @"Theft4FrameScheduling": @YES,
        @"Theft4PrewarmTargetReuse": @YES,
        @"Theft4RendererEfficiency": @YES,
        @"Theft4ParallelPreparation": @YES,
        @"Theft4MemoryRecovery": @YES,
        @"Theft4CommandStream": @YES,
        @"Theft4CpuCleanup": @YES,
        @"Theft4FrameAssembly": @YES,
        @"Theft4GraphicsPreparation": @YES,
        @"Theft4FusedSmaa": @YES,
        @"Theft4HardwareSmaa": @YES,
        @"Theft4FrameResourceSharing": @YES,
        @"Theft4ParallelTextureConversion": @YES,
        @"Theft4DetailedPerformanceCapture": @NO
    }];
    // Build 65 uses build 44's indices. Migrate those persisted choices once,
    // independently of the retired 0.2.1 migration markers. Never apply Auto here.
    NSUserDefaults *graphicsDefaults = NSUserDefaults.standardUserDefaults;
    if (![graphicsDefaults boolForKey:@"Theft4GraphicsIndicesMigrationBuild66"]) {
        NSDictionary *stored = [graphicsDefaults persistentDomainForName:NSBundle.mainBundle.bundleIdentifier];
        for (NSString *key in @[@"Theft4ShadowQuality", @"Theft4DrawDistance", @"Theft4ModelDetail"]) {
            NSNumber *value = stored[key];
            if (value) {
                NSInteger maximum = [key isEqualToString:@"Theft4ModelDetail"] ? 1 : 2;
                [graphicsDefaults setInteger:MAX(0, MIN(maximum, value.integerValue)) + 1 forKey:key];
            }
        }
        [graphicsDefaults setBool:YES forKey:@"Theft4GraphicsIndicesMigrationBuild66"];
    }
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
    _showCPUUsage = _bringupOverlay.showCPUUsage;
    _showFrameTime = _bringupOverlay.showFrameTime;
    _showFPS = _bringupOverlay.showFPS; _showControls = _bringupOverlay.showControls;
    _anisotropicFiltering = _bringupOverlay.anisotropicFiltering;
    _enhancedOutput = _bringupOverlay.enhancedOutput; _fsrBoost = _bringupOverlay.fsrBoost;
    _motionBlur = _bringupOverlay.motionBlur;
    _depthOfField = _bringupOverlay.depthOfField;
    _shadowQuality = _bringupOverlay.shadowQuality;
    _drawDistance = _bringupOverlay.drawDistance;
    _modelDetail = _bringupOverlay.modelDetail;
    _reflectionQuality = _bringupOverlay.reflectionQuality;
    _antiAliasing = _bringupOverlay.antiAliasing;
    _performanceCapture = _bringupOverlay.performanceCapture;
    _graphicsPreparation = _bringupOverlay.graphicsPreparation;
    _graphicsPreparation.on = [NSUserDefaults.standardUserDefaults boolForKey:@"Theft4GraphicsPreparation"];
    [_graphicsPreparation addTarget:self action:@selector(graphicsPreparationChanged:) forControlEvents:UIControlEventValueChanged];
    _fusedSmaa = _bringupOverlay.fusedSmaa;
    _fusedSmaa.on = [NSUserDefaults.standardUserDefaults boolForKey:@"Theft4FusedSmaa"];
    [_fusedSmaa addTarget:self action:@selector(fusedSmaaChanged:) forControlEvents:UIControlEventValueChanged];
    _frameResourceSharing = _bringupOverlay.frameResourceSharing;
    _frameResourceSharing.on = [NSUserDefaults.standardUserDefaults boolForKey:@"Theft4FrameResourceSharing"];
    [_frameResourceSharing addTarget:self action:@selector(frameResourceSharingChanged:) forControlEvents:UIControlEventValueChanged];
    _hardwareSmaa = _bringupOverlay.hardwareSmaa;
    _hardwareSmaa.on = [NSUserDefaults.standardUserDefaults boolForKey:@"Theft4HardwareSmaa"];
    [_hardwareSmaa addTarget:self action:@selector(hardwareSmaaChanged:) forControlEvents:UIControlEventValueChanged];
    _frameAssembly = _bringupOverlay.frameAssembly;
    _frameAssembly.on = [NSUserDefaults.standardUserDefaults boolForKey:@"Theft4FrameAssembly"];
    [_frameAssembly addTarget:self action:@selector(frameAssemblyChanged:) forControlEvents:UIControlEventValueChanged];
    _commandStream = _bringupOverlay.commandStream;
    _commandStream.on = [NSUserDefaults.standardUserDefaults boolForKey:@"Theft4CommandStream"];
    [_commandStream addTarget:self action:@selector(commandStreamChanged:) forControlEvents:UIControlEventValueChanged];
    _cpuCleanup = _bringupOverlay.cpuCleanup;
    _cpuCleanup.on = [NSUserDefaults.standardUserDefaults boolForKey:@"Theft4CpuCleanup"];
    [_cpuCleanup addTarget:self action:@selector(cpuCleanupChanged:) forControlEvents:UIControlEventValueChanged];
    _memoryRecovery = _bringupOverlay.memoryRecovery;
    _memoryRecovery.on = [NSUserDefaults.standardUserDefaults boolForKey:@"Theft4MemoryRecovery"];
    [_memoryRecovery addTarget:self action:@selector(memoryRecoveryChanged:) forControlEvents:UIControlEventValueChanged];
    _parallelTextureConversion = _bringupOverlay.parallelTextureConversion;
    _parallelTextureConversion.on = [NSUserDefaults.standardUserDefaults boolForKey:@"Theft4ParallelTextureConversion"];
    [_parallelTextureConversion addTarget:self action:@selector(parallelTextureConversionChanged:) forControlEvents:UIControlEventValueChanged];
    _parallelPreparation = _bringupOverlay.parallelPreparation;
    _parallelPreparation.on = [NSUserDefaults.standardUserDefaults boolForKey:@"Theft4ParallelPreparation"];
    [_parallelPreparation addTarget:self action:@selector(parallelPreparationChanged:)
        forControlEvents:UIControlEventValueChanged];
    _rendererEfficiency = _bringupOverlay.rendererEfficiency;
    _rendererEfficiency.on = [NSUserDefaults.standardUserDefaults boolForKey:@"Theft4RendererEfficiency"];
    [_rendererEfficiency addTarget:self action:@selector(rendererEfficiencyChanged:)
        forControlEvents:UIControlEventValueChanged];
    _prewarmTargetReuse = _bringupOverlay.prewarmTargetReuse;
    _prewarmTargetReuse.on = [NSUserDefaults.standardUserDefaults boolForKey:@"Theft4PrewarmTargetReuse"];
    [_prewarmTargetReuse addTarget:self action:@selector(prewarmTargetReuseChanged:)
        forControlEvents:UIControlEventValueChanged];
    _frameScheduling = _bringupOverlay.frameScheduling;
    _frameScheduling.on = [NSUserDefaults.standardUserDefaults boolForKey:@"Theft4FrameScheduling"];
    rex_frame_scheduling_set_mode(_frameScheduling.on);
    [_frameScheduling addTarget:self action:@selector(frameSchedulingChanged:)
        forControlEvents:UIControlEventValueChanged];
    _directGuestClock = _bringupOverlay.directGuestClock;
    _directGuestClock.on = [NSUserDefaults.standardUserDefaults boolForKey:@"Theft4DirectGuestClock"];
    [_directGuestClock addTarget:self action:@selector(directGuestClockChanged:)
        forControlEvents:UIControlEventValueChanged];
    _runtimeWaitImprovements = _bringupOverlay.runtimeWaitImprovements;
    _runtimeWaitImprovements.on = [NSUserDefaults.standardUserDefaults boolForKey:@"Theft4RuntimeWaitImprovements"];
    [_runtimeWaitImprovements addTarget:self action:@selector(runtimeWaitImprovementsChanged:)
        forControlEvents:UIControlEventValueChanged];
    _downloadLogButton = _bringupOverlay.downloadLogButton;
    [_downloadLogButton addTarget:self action:@selector(downloadLatestLogCapture)
                 forControlEvents:UIControlEventTouchUpInside];
    NSArray *toggles = @[_showCPUUsage,_showFrameTime,_showFPS,_showControls,_anisotropicFiltering,_enhancedOutput,_fsrBoost,
                         _motionBlur,_depthOfField,_performanceCapture];
    NSArray *keys = @[@"Theft4ShowCPUUsage",@"Theft4ShowFrameTime",@"Theft4ShowFPS",@"Theft4ShowTouchControls",@"Theft4AnisotropicFiltering",
                      @"Theft4EnhancedOutput1080p",@"Theft4ExperimentalFSRBoost",@"Theft4MotionBlur",
                      @"Theft4DepthOfField",@"Theft4DetailedPerformanceCapture"];
    for (NSUInteger i=0;i<toggles.count;++i) {
        UISwitch *toggle = toggles[i];
        toggle.on = [NSUserDefaults.standardUserDefaults boolForKey:keys[i]];
        [toggle addTarget:self action:@selector(displaySettingsChanged:) forControlEvents:UIControlEventValueChanged];
    }
    [_bringupOverlay.originalPresetButton addTarget:self action:@selector(applyOriginalGraphicsPreset)
        forControlEvents:UIControlEventTouchUpInside];
    _performanceCapture.on = NO;
    [NSUserDefaults.standardUserDefaults removeObjectForKey:@"Theft4DetailedPerformanceCapture"];
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
            height == 540 ? 0 : height == 900 ? 2 : height == 1080 ? 3 :
            height == THEFT4_LAB_NATIVE_16_9 ? 4 : 1;
        if (![defaults objectForKey:@"Theft4LabFSREnabled"])
            [defaults setBool:_enhancedOutput.on forKey:@"Theft4LabFSREnabled"];
        _bringupOverlay.fsrUpscaling.on = [defaults boolForKey:@"Theft4LabFSREnabled"];
        [self applyLimitedMemoryCaps];
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
    _cpuUsageView = [Theft4CPUUsageView new];
    _cpuUsageView.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_cpuUsageView];
    [NSLayoutConstraint activateConstraints:@[
        [_cpuUsageView.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor constant:10],
        [_cpuUsageView.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor constant:10],
        [_cpuUsageView.widthAnchor constraintEqualToConstant:236],
        [_cpuUsageView.heightAnchor constraintEqualToConstant:194]]];
    __weak Theft4ViewController *cpuController = self;
    _cpuUsageView.sampleLogHandler = ^(NSString *rows) {
        Theft4ViewController *controller = cpuController;
        if (!controller || !controller->_publicationCaptureActive ||
            !controller->_publicationCaptureQueue) return;
        dispatch_async(controller->_publicationCaptureQueue, ^{
            [controller appendPublicationCaptureText:rows];
        });
    };
    _frameTimeView = [Theft4FrameTimeView new];
    _frameTimeView.translatesAutoresizingMaskIntoConstraints = NO;
    _frameTimeView.hidden = YES;
    UITapGestureRecognizer *capture = [[UITapGestureRecognizer alloc]
        initWithTarget:self action:@selector(requestNativeProfile)];
    capture.numberOfTapsRequired = 2;
    [_frameTimeView addGestureRecognizer:capture];
    UILongPressGestureRecognizer *marker = [[UILongPressGestureRecognizer alloc]
        initWithTarget:self action:@selector(markPerformanceScene:)];
    [_frameTimeView addGestureRecognizer:marker];
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
    [NSRunLoop.mainRunLoop addTimer:_fpsTimer forMode:NSRunLoopCommonModes];
    NSError *error = nil;
    NSURL *support = [NSFileManager.defaultManager URLForDirectory:NSApplicationSupportDirectory
        inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:&error];
    _supportURL = [support URLByAppendingPathComponent:@"Theft4" isDirectory:YES];
    if (!_supportURL || ![NSFileManager.defaultManager createDirectoryAtURL:_supportURL
        withIntermediateDirectories:YES attributes:nil error:&error]) {
        _failure = error.localizedDescription ?: @"Application Support is unavailable";
    } else {
        _logURL = [_supportURL URLByAppendingPathComponent:@"lifecycle.jsonl"];
        NSURL *faultDirectory = [_supportURL URLByAppendingPathComponent:@"faults" isDirectory:YES];
        if ([NSFileManager.defaultManager createDirectoryAtURL:faultDirectory withIntermediateDirectories:YES attributes:nil error:nil]) {
            NSURL *faultURL = [faultDirectory URLByAppendingPathComponent:
                [NSString stringWithFormat:@"fault-%@.bin", NSUUID.UUID.UUIDString]];
            RexInitializeFaultDiagnostics(faultURL.fileSystemRepresentation);
            RexSetFaultSnapshotWriter(rex_gta4_light_capture_write_fault_snapshot);
        }
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

- (void)chooseTitleUpdate {
    if (!_gameURL) return;
    const BOOL hasBase = [NSFileManager.defaultManager
        fileExistsAtPath:[[_gameURL URLByAppendingPathComponent:@"default.xex"] path]];
    if (!hasBase) {
        [self routeInstallationFlow];
        return;
    }
    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc]
        initWithDocumentTypes:@[@"public.data"] inMode:UIDocumentPickerModeImport];
    picker.delegate = self;
    picker.allowsMultipleSelection = NO;
    [self presentViewController:picker animated:YES completion:nil];
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller
didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    (void)controller;
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
    (void)controller;
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

- (void)setFrameSchedulingEnabled:(BOOL)enabled {
    _frameScheduling.on = enabled;
    [NSUserDefaults.standardUserDefaults setBool:enabled forKey:@"Theft4FrameScheduling"];
    rex_frame_scheduling_set_mode(enabled);
    const uint64_t generation = rex_frame_scheduling_generation();
    [self record:[NSString stringWithFormat:@"frame-scheduling.requested=%d generation=%llu",
        enabled, (unsigned long long)generation]];
    if (_publicationCaptureActive) {
        const uint64_t now = (uint64_t)(CACurrentMediaTime() * 1e9);
        dispatch_async(_publicationCaptureQueue, ^{
            [self appendPublicationCaptureText:[NSString stringWithFormat:
                @"marker,,%llu,,frame-scheduling-requested=%d generation=%llu\n",
                (unsigned long long)now, enabled, (unsigned long long)generation]];
        });
    }
}

- (void)frameSchedulingChanged:(UISwitch *)sender {
    [self setFrameSchedulingEnabled:sender.on];
}

- (void)performanceProfileChanged:(NSNotification *)notification {
    NSString *fields = Theft4PerformanceProfileFields();
    const uint64_t timestamp = (uint64_t)(CACurrentMediaTime() * 1e9);
    dispatch_async(dispatch_get_main_queue(), ^{
        [self record:[@"performance.profile_changed " stringByAppendingString:fields]];
        if (!self->_publicationCaptureActive) return;
        NSString *row = [NSString stringWithFormat:@"marker,,%llu,,%@\n",
            (unsigned long long)timestamp, fields];
        dispatch_async(self->_publicationCaptureQueue, ^{ [self appendPublicationCaptureText:row]; });
    });
}

- (void)commandStreamChanged:(UISwitch *)sender {
    [NSUserDefaults.standardUserDefaults setBool:sender.on forKey:@"Theft4CommandStream"];
}
- (void)cpuCleanupChanged:(UISwitch *)sender {
    [NSUserDefaults.standardUserDefaults setBool:sender.on forKey:@"Theft4CpuCleanup"];
}
- (void)memoryRecoveryChanged:(UISwitch *)sender {
    [NSUserDefaults.standardUserDefaults setBool:sender.on forKey:@"Theft4MemoryRecovery"];
}

- (void)graphicsPreparationChanged:(UISwitch *)sender {
    [NSUserDefaults.standardUserDefaults setBool:sender.on forKey:@"Theft4GraphicsPreparation"];
}
- (void)fusedSmaaChanged:(UISwitch *)sender {
    [NSUserDefaults.standardUserDefaults setBool:sender.on forKey:@"Theft4FusedSmaa"];
}
- (void)frameResourceSharingChanged:(UISwitch *)sender {
    [NSUserDefaults.standardUserDefaults setBool:sender.on forKey:@"Theft4FrameResourceSharing"];
}
- (void)hardwareSmaaChanged:(UISwitch *)sender {
    [NSUserDefaults.standardUserDefaults setBool:sender.on forKey:@"Theft4HardwareSmaa"];
}
- (void)frameAssemblyChanged:(UISwitch *)sender {
    [NSUserDefaults.standardUserDefaults setBool:sender.on forKey:@"Theft4FrameAssembly"];
}
- (void)parallelTextureConversionChanged:(UISwitch *)sender {
    [NSUserDefaults.standardUserDefaults setBool:sender.on forKey:@"Theft4ParallelTextureConversion"];
}
- (void)parallelPreparationChanged:(UISwitch *)sender {
    [NSUserDefaults.standardUserDefaults setBool:sender.on forKey:@"Theft4ParallelPreparation"];
}

- (void)rendererEfficiencyChanged:(UISwitch *)sender {
    [NSUserDefaults.standardUserDefaults setBool:sender.on forKey:@"Theft4RendererEfficiency"];
}

- (void)prewarmTargetReuseChanged:(UISwitch *)sender {
    [NSUserDefaults.standardUserDefaults setBool:sender.on forKey:@"Theft4PrewarmTargetReuse"];
}

- (void)directGuestClockChanged:(UISwitch *)sender {
    [NSUserDefaults.standardUserDefaults setBool:sender.on forKey:@"Theft4DirectGuestClock"];
}

- (void)runtimeWaitImprovementsChanged:(UISwitch *)sender {
    [NSUserDefaults.standardUserDefaults setBool:sender.on forKey:@"Theft4RuntimeWaitImprovements"];
}

- (void)displaySettingsChanged:(UIControl *)sender {
    if (sender) [NSUserDefaults.standardUserDefaults setObject:@"custom" forKey:@"Theft4GraphicsPreset"];
    if (_bringupOverlay.renderResolution.selectedSegmentIndex == 4) _bringupOverlay.fsrUpscaling.on = NO;
    [self applyLimitedMemoryCaps];
    if (sender == _fsrBoost && _fsrBoost.on) _enhancedOutput.on = YES;
    if (sender == _enhancedOutput && !_enhancedOutput.on) _fsrBoost.on = NO;
    [NSUserDefaults.standardUserDefaults setBool:_performanceCapture.on
                                          forKey:@"Theft4DetailedPerformanceCapture"];
    [NSUserDefaults.standardUserDefaults setBool:_fsrBoost.on forKey:@"Theft4ExperimentalFSRBoost"];
    [NSUserDefaults.standardUserDefaults setBool:_motionBlur.on forKey:@"Theft4MotionBlur"];
    [NSUserDefaults.standardUserDefaults setBool:_depthOfField.on forKey:@"Theft4DepthOfField"];
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
    [NSUserDefaults.standardUserDefaults setBool:_showCPUUsage.on forKey:@"Theft4ShowCPUUsage"];
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

- (void)applyOriginalGraphicsChoices {
    if (!_bringupOverlay.renderResolution || _executionAttempted) return;
    _bringupOverlay.renderResolution.selectedSegmentIndex = 1;
    _bringupOverlay.fsrUpscaling.on = NO;
    _shadowQuality.selectedSegmentIndex = 1;
    _drawDistance.selectedSegmentIndex = 1;
    _modelDetail.selectedSegmentIndex = 1;
    _reflectionQuality.selectedSegmentIndex = 0;
    _antiAliasing.selectedSegmentIndex = 0;
    _anisotropicFiltering.on = NO;
    _motionBlur.on = YES;
    _depthOfField.on = YES;
    _enhancedOutput.on = NO;
    _fsrBoost.on = NO;
}

- (void)applyOriginalGraphicsPreset {
    if (!_bringupOverlay.renderResolution || _executionAttempted) return;
    [self applyOriginalGraphicsChoices];
    [self displaySettingsChanged:nil];
    [NSUserDefaults.standardUserDefaults setObject:@"original" forKey:@"Theft4GraphicsPreset"];
}

- (void)applyLowPowerPreset {
    if (!_bringupOverlay.renderResolution || _executionAttempted) return;
    [self applyOriginalGraphicsChoices];
    switch (automaticGraphicsTier()) {
        case Theft4AutomaticGraphicsTierLow:
            _bringupOverlay.renderResolution.selectedSegmentIndex = 0;
            _bringupOverlay.fsrUpscaling.on = YES;
            _shadowQuality.selectedSegmentIndex = 1; // Optimized requires shadow cache validation.
            _drawDistance.selectedSegmentIndex = 0;
            _motionBlur.on = NO;
            _depthOfField.on = NO;
            break;
        case Theft4AutomaticGraphicsTierIPhone17Pro:
            _bringupOverlay.renderResolution.selectedSegmentIndex = 2;
            _bringupOverlay.fsrUpscaling.on = YES;
            _shadowQuality.selectedSegmentIndex = 2;
            _antiAliasing.selectedSegmentIndex = 2;
            _anisotropicFiltering.on = YES;
            _motionBlur.on = NO;
            _depthOfField.on = NO;
            break;
        case Theft4AutomaticGraphicsTierM5IPad:
            _bringupOverlay.renderResolution.selectedSegmentIndex = 3;
            _bringupOverlay.fsrUpscaling.on = YES;
            _shadowQuality.selectedSegmentIndex = 2;
            _modelDetail.selectedSegmentIndex = 2;
            _antiAliasing.selectedSegmentIndex = 2;
            _anisotropicFiltering.on = YES;
            _motionBlur.on = NO;
            break;
        case Theft4AutomaticGraphicsTierOriginal:
            break;
    }
    [self displaySettingsChanged:nil];
    [NSUserDefaults.standardUserDefaults setObject:@"auto" forKey:@"Theft4GraphicsPreset"];
}

- (void)applyLimitedMemoryCaps {
    if (!_limitedMemoryProfile || !_bringupOverlay.renderResolution || _executionAttempted) return;

    // Devices with 6 GiB or less stay inside a 900p scene / 1080p presentation
    // ceiling, with settings that add render targets, samples, or draws removed.
    if (_bringupOverlay.renderResolution.selectedSegmentIndex > 2)
        _bringupOverlay.renderResolution.selectedSegmentIndex = 2;
    _bringupOverlay.fsrUpscaling.on = YES;
    if (_shadowQuality.selectedSegmentIndex > 1) _shadowQuality.selectedSegmentIndex = 1;
    _drawDistance.selectedSegmentIndex = 0;
    if (_modelDetail.selectedSegmentIndex > 1) _modelDetail.selectedSegmentIndex = 1;
    _reflectionQuality.selectedSegmentIndex = 0;
    _antiAliasing.selectedSegmentIndex = 0;
    _anisotropicFiltering.on = NO;
    _motionBlur.on = NO;
    _depthOfField.on = NO;
    _fsrBoost.on = NO;
    _enhancedOutput.on = YES;

    [_bringupOverlay.renderResolution setEnabled:NO forSegmentAtIndex:3];
    [_bringupOverlay.renderResolution setEnabled:NO forSegmentAtIndex:4];
    for (NSInteger index = 2; index < _shadowQuality.numberOfSegments; ++index)
        [_shadowQuality setEnabled:NO forSegmentAtIndex:index];
    for (NSInteger index = 1; index < _drawDistance.numberOfSegments; ++index)
        [_drawDistance setEnabled:NO forSegmentAtIndex:index];
    if (_modelDetail.numberOfSegments > 2) [_modelDetail setEnabled:NO forSegmentAtIndex:2];
    for (NSInteger index = 1; index < _reflectionQuality.numberOfSegments; ++index)
        [_reflectionQuality setEnabled:NO forSegmentAtIndex:index];
    for (NSInteger index = 1; index < _antiAliasing.numberOfSegments; ++index)
        [_antiAliasing setEnabled:NO forSegmentAtIndex:index];
    _anisotropicFiltering.enabled = NO;
    _motionBlur.enabled = NO;
    _depthOfField.enabled = NO;
    _fsrBoost.enabled = NO;
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
        // Preserve the newest lightweight/stage capture before older deep
        // profiles consume the bounded export budget. The normal enumeration
        // below skips this already-included file, without deleting anything.
        NSURL *frameDirectory = [startupURL URLByAppendingPathComponent:@"frame-captures" isDirectory:YES];
        NSURL *latestFrameCapture = nil;
        NSDate *latestFrameDate = NSDate.distantPast;
        for (NSURL *candidate in [fm contentsOfDirectoryAtURL:frameDirectory
            includingPropertiesForKeys:@[NSURLContentModificationDateKey, NSURLIsRegularFileKey]
            options:NSDirectoryEnumerationSkipsHiddenFiles error:nil]) {
            if (![candidate.lastPathComponent hasPrefix:@"publication-trace-"] ||
                ![candidate.pathExtension isEqualToString:@"csv"]) continue;
            NSNumber *regular = nil;
            NSDate *modified = nil;
            [candidate getResourceValue:&regular forKey:NSURLIsRegularFileKey error:nil];
            [candidate getResourceValue:&modified forKey:NSURLContentModificationDateKey error:nil];
            if (regular.boolValue && modified && [modified compare:latestFrameDate] == NSOrderedDescending) {
                latestFrameCapture = candidate;
                latestFrameDate = modified;
            }
        }
        if (latestFrameCapture) {
            success = success && Theft4AppendDiagnosticFile(stream, latestFrameCapture,
                [@"Theft4/startup/frame-captures/" stringByAppendingString:latestFrameCapture.lastPathComponent],
                &budget, included, skipped);
            NSURL *timingCapture = [frameDirectory URLByAppendingPathComponent:
                [latestFrameCapture.lastPathComponent stringByReplacingOccurrencesOfString:@"publication-trace-" withString:@"renderer-timing-"]];
            if ([fm fileExistsAtPath:timingCapture.path]) success = success && Theft4AppendDiagnosticFile(stream,
                timingCapture, [@"Theft4/startup/frame-captures/" stringByAppendingString:timingCapture.lastPathComponent],
                &budget, included, skipped);
            NSURL *callerCapture = [frameDirectory URLByAppendingPathComponent:
                [latestFrameCapture.lastPathComponent stringByReplacingOccurrencesOfString:@"publication-trace-" withString:@"runtime-callers-"]];
            if ([fm fileExistsAtPath:callerCapture.path]) success = success && Theft4AppendDiagnosticFile(stream,
                callerCapture, [@"Theft4/startup/frame-captures/" stringByAppendingString:callerCapture.lastPathComponent],
                &budget, included, skipped);
            NSURL *schedulingCapture = [frameDirectory URLByAppendingPathComponent:
                [latestFrameCapture.lastPathComponent stringByReplacingOccurrencesOfString:@"publication-trace-" withString:@"frame-scheduling-"]];
            if ([fm fileExistsAtPath:schedulingCapture.path]) success = success && Theft4AppendDiagnosticFile(stream,
                schedulingCapture, [@"Theft4/startup/frame-captures/" stringByAppendingString:schedulingCapture.lastPathComponent],
                &budget, included, skipped);
        }
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
    [_cpuUsageView setMonitoringActive:(_gamePresentation && _sceneActive && _showCPUUsage.on)];
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

- (void)appendPublicationCaptureText:(NSString *)text {
    if (!_publicationCaptureURL || !text.length) return;
    NSData *encoded = [text dataUsingEncoding:NSUTF8StringEncoding];
    if (_publicationCaptureBytes + encoded.length > 20 * 1024 * 1024) {
        dispatch_async(dispatch_get_main_queue(), ^{ [self stopPublicationCapture]; });
        return;
    }
    _publicationCaptureBytes += encoded.length;
    NSError *error = nil;
    NSFileHandle *file = [NSFileHandle fileHandleForWritingToURL:_publicationCaptureURL
                                                          error:&error];
    if (file) {
        [file seekToEndOfFile];
        if (![file writeData:[text dataUsingEncoding:NSUTF8StringEncoding] error:&error]) {
            [file closeFile];
        } else {
            [file closeFile];
            return;
        }
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        [self stopPublicationCapture];
        self->_publicationCaptureWriteFailed = YES;
        [self record:[NSString stringWithFormat:@"capture.lightweight_write_failed: %@",
            error.localizedDescription ?: @"unknown error"]];
    });
}

// Scheduling/audio samples are collected into bounded RAM during gameplay.
// All file writes remain on explicit save/background, using this serial queue.
- (void)collectFrameScheduling {
    if (_schedulingFull || !_schedulingRows) return;
    rex_frame_scheduling_sample rows[128];
    uint32_t cursor = 0;
    for (unsigned batch = 0; batch < 64; ++batch) {
        const uint32_t count = rex_frame_scheduling_read(&cursor, rows, 128);
        NSMutableString *text = [NSMutableString new];
        for (uint32_t i = 0; i < count; ++i) {
            for (unsigned field = 0; field < REX_FRAME_SCHEDULING_FIELDS; ++field) {
                if (field) [text appendString:@","];
                if (field == 6 || field == 11 || field == 13)
                    [text appendFormat:@"%lld", (long long)rows[i].value[field]];
                else [text appendFormat:@"%llu", (unsigned long long)rows[i].value[field]];
            }
            [text appendString:@"\n"];
        }
        if (_schedulingBytes + text.length > 4 * 1024 * 1024) {
            _schedulingFull = YES;
            rex_frame_scheduling_capture(0);
            [_schedulingRows appendString:@"# capacity_reached: observations stopped; scheduling mode unchanged\n"];
            break;
        }
        _schedulingBytes += text.length;
        [_schedulingRows appendString:text];
        if (count < 128) break;
    }
}

- (void)flushFrameScheduling {
    if (!_schedulingRows) return;
    NSFileHandle *file = [NSFileHandle fileHandleForWritingToURL:_schedulingURL error:nil];
    @try {
        if (!file) @throw [NSException exceptionWithName:@"SchedulingExport" reason:@"Unable to open scheduling file" userInfo:nil];
        [file seekToEndOfFile];
        [file writeData:[_schedulingRows dataUsingEncoding:NSUTF8StringEncoding]];
        NSString *status = [NSString stringWithFormat:@"# snapshot_ns=%llu dropped_records=%llu\n",
            (unsigned long long)(CACurrentMediaTime() * 1e9),
            (unsigned long long)rex_frame_scheduling_dropped()];
        [file writeData:[status dataUsingEncoding:NSUTF8StringEncoding]];
        [file closeFile];
        [_schedulingRows setString:@""];
    } @catch (NSException *exception) {
        [file closeFile];
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_publicationCaptureWriteFailed = YES;
            [self record:@"capture.scheduling_export_failed"];
            UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"CAPTURE SAVE FAILED"
                message:@"Scheduling and audio observations could not be saved. Leave Theft4 open so they can be recovered."
                preferredStyle:UIAlertControllerStyleAlert];
            [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
            [self presentViewController:alert animated:YES completion:nil];
        });
    }
}

// Runs only on the capture export queue, at most once per second. This is
// sampled wall time (including descheduling), not CPU time or a full stack trace.
- (void)collectRuntimeCallers {
    [self collectFrameScheduling];
    if (_runtimeCallerFull || !_runtimeCallerRows) return;
    const uint64_t now = (uint64_t)(CACurrentMediaTime() * 1e9);
    const uint64_t frame = theft4_frame_counter_published_frames();
    rex_runtime_caller_sample rows[128];
    uint32_t cursor = 0;
    NSMutableString *text = [NSMutableString new];
    for (;;) {
        const uint32_t count = rex_runtime_callers_read(&cursor, rows, 128);
        for (uint32_t i = 0; i < count; ++i) {
            const rex_runtime_caller_sample *row = &rows[i];
            [text appendFormat:@"%llu,%llu,%llu,%llu,0x%llx,%llu,%llu,%llu,%llu\n",
                (unsigned long long)now, (unsigned long long)frame,
                (unsigned long long)row->thread_id, (unsigned long long)row->kind,
                (unsigned long long)row->site, (unsigned long long)row->calls,
                (unsigned long long)row->samples, (unsigned long long)row->wall_ticks,
                (unsigned long long)row->max_wall_ticks];
        }
        if (count < 128) break;
    }
    [text appendFormat:@"# snapshot_ns=%llu dropped_calls=%llu\n",
        (unsigned long long)now, (unsigned long long)rex_runtime_callers_dropped()];
    if (_runtimeCallerBytes + text.length > 8 * 1024 * 1024) {
        _runtimeCallerFull = YES;
        rex_runtime_callers_enable(0);
        [_runtimeCallerRows appendString:@"# capacity_reached: caller observations stopped at 8 MiB\n"];
    } else {
        _runtimeCallerBytes += text.length; // ASCII only
        [_runtimeCallerRows appendString:text];
    }
}

- (void)flushRuntimeCallers {
    [self collectRuntimeCallers];
    [self flushFrameScheduling];
    if (!_runtimeCallerRows.length) return;
    NSFileHandle *file = [NSFileHandle fileHandleForWritingToURL:_runtimeCallerURL error:nil];
    @try {
        if (!file) @throw [NSException exceptionWithName:@"CallerExport" reason:@"Unable to open caller file" userInfo:nil];
        [file seekToEndOfFile];
        [file writeData:[_runtimeCallerRows dataUsingEncoding:NSUTF8StringEncoding]];
        [file closeFile];
        [_runtimeCallerRows setString:@""];
    } @catch (NSException *exception) {
        [file closeFile];
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_publicationCaptureWriteFailed = YES;
            [self record:@"capture.caller_export_failed"];
            UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"CALLER CAPTURE SAVE FAILED"
                message:@"Caller observations could not be saved. Leave the app open so they can be recovered."
                preferredStyle:UIAlertControllerStyleAlert];
            [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
            [self presentViewController:alert animated:YES completion:nil];
        });
    }
}

- (void)drainPublicationCapture {
    if (!_publicationCaptureActive || !_publicationCaptureQueue) return;
    dispatch_async(_publicationCaptureQueue, ^{
        [self flushRuntimeCallers];
        theft4_publication_sample samples[256];
        NSMutableString *text = [NSMutableString new];
        uint64_t lost = 0;
        uint64_t lostTotal = 0;
        for (unsigned batch = 0; batch < 64; ++batch) {
            const uint32_t count = theft4_publication_capture_read(
                &self->_publicationCaptureCursor, samples, 256, &lost);
            lostTotal += lost;
            for (uint32_t i = 0; i < count; ++i)
                [text appendFormat:@"frame,%llu,%llu,,\n",
                    (unsigned long long)samples[i].frame,
                    (unsigned long long)samples[i].monotonic_ns];
            if (count < 256) break;
        }
        if (lostTotal) [text appendFormat:@"lost,,,%llu,\n", (unsigned long long)lostTotal];
        const theft4_output_policy output = theft4_metal_get_output_policy();
        [text appendFormat:@"context,,%llu,,thermal=%ld render=%ux%u output=%ux%u\n",
            (unsigned long long)(CACurrentMediaTime() * 1e9),
            (long)NSProcessInfo.processInfo.thermalState,
            output.render_width, output.render_height, output.output_width, output.output_height];
        [self appendPublicationCaptureText:text];
        NSMutableString *timings = [NSMutableString new];
        rex_light_sample timingSamples[128];
        uint64_t timingLost = 0;
        for (unsigned batch = 0; batch < 128; ++batch) {
            uint64_t lost = 0;
            const uint32_t count = rex_gta4_light_capture_read(&self->_lightCaptureCursor, timingSamples, 128, &lost);
            timingLost += lost;
            for (uint32_t row = 0; row < count; ++row) {
                for (unsigned field = 0; field < REX_LIGHT_FIELDS; ++field)
                    [timings appendFormat:field ? @",%llu" : @"%llu", (unsigned long long)timingSamples[row].value[field]];
                [timings appendString:@"\n"];
            }
            if (count < 128) break;
        }
        if (timingLost) [timings appendFormat:@"# lost_records=%llu\n", (unsigned long long)timingLost];
        NSFileHandle *timingFile = [NSFileHandle fileHandleForWritingToURL:self->_lightCaptureURL error:nil];
        @try {
            if (!timingFile) @throw [NSException exceptionWithName:@"TimingExport" reason:@"Unable to open timing file" userInfo:nil];
            [timingFile seekToEndOfFile];
            [timingFile writeData:[timings dataUsingEncoding:NSUTF8StringEncoding]];
            [timingFile closeFile];
        } @catch (NSException *exception) {
            [timingFile closeFile];
            dispatch_async(dispatch_get_main_queue(), ^{
                self->_publicationCaptureWriteFailed = YES;
                [self record:@"capture.timing_export_failed"];
                UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"CAPTURE SAVE FAILED"
                    message:@"Renderer timings could not be saved. Please export logs before closing."
                    preferredStyle:UIAlertControllerStyleAlert];
                [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
                [self presentViewController:alert animated:YES completion:nil];
            });
        }
    });
}

- (BOOL)beginPublicationCapture {
    if (_publicationCaptureActive) return YES;
    if (!_supportURL || _publicationCaptureWriteFailed || _publicationCaptureURL) return NO;
    NSURL *directory = [[_supportURL URLByAppendingPathComponent:@"startup" isDirectory:YES]
        URLByAppendingPathComponent:@"frame-captures" isDirectory:YES];
    NSError *error = nil;
    if (![NSFileManager.defaultManager createDirectoryAtURL:directory
        withIntermediateDirectories:YES attributes:nil error:&error]) return NO;
    NSDateFormatter *formatter = [NSDateFormatter new];
    formatter.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
    formatter.dateFormat = @"yyyy-MM-dd-HH-mm-ss";
    _publicationCaptureURL = [directory URLByAppendingPathComponent:
        [NSString stringWithFormat:@"publication-trace-%@-%@.csv", [formatter stringFromDate:NSDate.date],
            [NSUUID.UUID.UUIDString substringToIndex:8]]];
    NSString *header = [NSString stringWithFormat:@"# build%@: runtime_wait_fixes=%d; direct_guest_clock=%d; publication timestamps, not scanout or GPU durations\nkind,frame,monotonic_ns,lost_count,note\nstatus,,,,collecting\n", NSBundle.mainBundle.infoDictionary[@"CFBundleVersion"], _runtimeWaitImprovements.on, _directGuestClock.on];
    header = [header stringByAppendingFormat:@"marker,,%llu,,%@\n",
        (unsigned long long)(CACurrentMediaTime() * 1e9), Theft4PerformanceProfileFields()];
    if (![header writeToURL:_publicationCaptureURL atomically:YES encoding:NSUTF8StringEncoding error:&error]) {
        _publicationCaptureURL = nil;
        [self record:@"capture.long_start_failed"];
        return NO;
    }
    _lightCaptureURL = [directory URLByAppendingPathComponent:
        [_publicationCaptureURL.lastPathComponent stringByReplacingOccurrencesOfString:@"publication-trace-" withString:@"renderer-timing-"]];
    NSString *lightHeader = [NSString stringWithFormat:
        @"# build%@: host_tick_frequency=%llu; CPU fields are ns; fence waits are not GPU durations; sample_valid flags1=CPU-publish 2=CPU-interval 4=memory 8=runtime-counters 16=task-events 32=decompressions 64=available-memory; sparse assembly1frame/60 with overlapping wall categories; pressure texture bytes mean retirement; cumulative counters may overlap; pipeline_creates/compile/wait are per-present; compiler/cache snapshots use bit8; phase IDs0=unknown1=scene2=lighting3=light-setup4=light-draw5=radar6=postfx; appended counts are recording observations; boundary metadata per-present; activity epochs reset CPU intervals; prewarm and renderer-efficiency counters cumulative; dynamic counts are state groups; preparation counters per-publication with overlapping helper work; helper_cpu_ns zero=unavailable; assembly and texture counters cumulative; texture wall spans overlap; prepared index counts per-publication; bounded16384 records\nframe,begin_tick,end_tick,commands,completion_ticks,fence_wait_ticks,preparation_ticks,recording_ticks,finalization_ticks,queue_lock_ticks,driver_submit_ticks,submission,slot,result,cpu_publish_ns,cpu_interval_ns,cpu_interval_ticks,sample_valid,footprint_bytes,resident_bytes,compressed_bytes,texture_images,memory_warnings,%s\n",
        NSBundle.mainBundle.infoDictionary[@"CFBundleVersion"],
        (unsigned long long)rex_gta4_light_capture_frequency(), rex_gta4_light_capture_extra_columns()];
    if (![lightHeader writeToURL:_lightCaptureURL atomically:YES encoding:NSUTF8StringEncoding error:&error]) {
        _lightCaptureURL = nil;
        [self record:@"capture.timing_start_failed"];
        return NO;
    }
    _runtimeCallerURL = [directory URLByAppendingPathComponent:
        [_publicationCaptureURL.lastPathComponent stringByReplacingOccurrencesOfString:@"publication-trace-" withString:@"runtime-callers-"]];
    NSString *callerHeader = [NSString stringWithFormat:
        @"# build%@; clock_direct=%d runtime_wait_fixes=%d; host_tick_frequency=%llu image_load_address=0x%llx; counters=cumulative; wall=sampled_every_64_calls_including_descheduling; snapshots=approximate; capacities=128_threads_64_sites_per_thread_8MiB; dropped_UINT64_MAX=thread_capacity_exceeded; kinds=1:native_clock_return_PC 2:legacy_clock_contention 3:native_yield_return_PC 4:guest_zero_delay_LR 5:guest_nonzero_delay_LR 6:guest_wait_LR 7:guest_multiwait_LR 8:guest_82849910_incoming_LR 9:guest_82A1A200_incoming_LR 10:guest_82193D80_incoming_LR 11:guest_signalwait_LR; nested_wall_samples_overlap; sampled_max_is_not_all_call_max\nmonotonic_ns,frame,thread_id,kind,site,calls,samples,wall_ticks,max_wall_ticks\n",
        NSBundle.mainBundle.infoDictionary[@"CFBundleVersion"],
        _directGuestClock.on, _runtimeWaitImprovements.on,
        (unsigned long long)rex_runtime_callers_frequency(),
        (unsigned long long)(uintptr_t)_dyld_get_image_header(0)];
    if (![callerHeader writeToURL:_runtimeCallerURL atomically:YES encoding:NSUTF8StringEncoding error:&error]) {
        [self record:@"capture.caller_start_failed"];
        return NO;
    }
    _schedulingURL = [directory URLByAppendingPathComponent:
        [_publicationCaptureURL.lastPathComponent stringByReplacingOccurrencesOfString:@"publication-trace-" withString:@"frame-scheduling-"]];
    NSString *schedulingHeader = [NSString stringWithFormat:
        @"# build%@ initial_mode=%d; times=ns; event=1:identity 2:qos_request 3:fixed_work 4:audio_work 5:witness_budget; roles=1:guest 2:main 4:native 8:present_producer 16:audio; work=1:register 2:buffer 3:audio_prepare 4:audio_mix 5:xma_work 6:xma_decode; units=fixed_iterations_or_cumulative_calls; audio_sampling=1/64_DSP_1/256_XMA; nested_scopes_overlap; sampled_call_counts_omit_unsampled_tail; in_flight_samples_may_finish_after_stop; requested_qos_excludes_override; result=POSIX_status; witness_max_threads=4; witness_counts_frozen=16384; witness_budget_per_thread=0.025pct_plus_2ms_startup; cpu_ns_zero=unavailable; bounded4MiB; dropped_UINT64_MAX=thread_capacity_exceeded\n%s\n",
        NSBundle.mainBundle.infoDictionary[@"CFBundleVersion"],
        rex_frame_scheduling_mode(), rex_frame_scheduling_columns()];
    if (![schedulingHeader writeToURL:_schedulingURL atomically:YES encoding:NSUTF8StringEncoding error:&error]) {
        [self record:@"capture.scheduling_start_failed"];
        return NO;
    }
    _schedulingRows = [NSMutableString new];
    _runtimeCallerRows = [NSMutableString new];
    _lightCaptureCursor = rex_gta4_light_capture_start();
    _publicationCaptureQueue = dispatch_queue_create("theft4.publication-capture",
        dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_UTILITY, 0));
    _runtimeCallerTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, _publicationCaptureQueue);
    dispatch_source_set_timer(_runtimeCallerTimer, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), NSEC_PER_SEC, NSEC_PER_SEC / 5);
    __weak typeof(self) weakSelf = self;
    dispatch_source_set_event_handler(_runtimeCallerTimer, ^{ [weakSelf collectRuntimeCallers]; });
    rex_frame_scheduling_capture(1);
    rex_runtime_callers_enable(1);
    dispatch_resume(_runtimeCallerTimer);
    _publicationCaptureCursor = theft4_publication_capture_start();
    _publicationCaptureStartTime = CACurrentMediaTime();
    _publicationCaptureActive = YES;
    [self record:@"capture.long_started"];
    return YES;
}

- (void)stopPublicationCapture {
    if (!_publicationCaptureActive) return;
    rex_frame_scheduling_capture(0);
    rex_runtime_callers_enable(0);
    if (_runtimeCallerTimer) { dispatch_source_cancel(_runtimeCallerTimer); _runtimeCallerTimer = nil; }
    theft4_publication_capture_stop();
    rex_gta4_light_capture_stop();
    [self drainPublicationCapture];
    _publicationCaptureActive = NO;
    dispatch_async(_publicationCaptureQueue, ^{ [self appendPublicationCaptureText:@"status,,,,saved\n"]; });
    // Drain already-started audio scopes once more without blocking the UI.
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC / 2), _publicationCaptureQueue, ^{
        [self collectFrameScheduling];
        [self flushFrameScheduling];
    });
    [self record:@"capture.long_stopped"];
}

- (void)markPerformanceScene:(UILongPressGestureRecognizer *)gesture {
    if (gesture.state != UIGestureRecognizerStateBegan) return;
    const BOOL scheduling = rex_frame_scheduling_mode() != 0;
    NSString *message = [NSString stringWithFormat:@"Frame Scheduling: %@. %@",
        scheduling ? @"On" : @"Original",
        _publicationCaptureActive ? @"Recording. Choose an action." : @"Enable Long Performance Capture before Play to record the next run."];
    UIAlertController *menu = [UIAlertController alertControllerWithTitle:@"PERFORMANCE"
        message:message
        preferredStyle:UIAlertControllerStyleActionSheet];
    [menu addAction:[UIAlertAction actionWithTitle:scheduling ? @"Use Original Scheduling" : @"Use Frame Scheduling"
        style:UIAlertActionStyleDefault handler:^(__unused UIAlertAction *action) {
            [self setFrameSchedulingEnabled:!scheduling];
        }]];
    if (_publicationCaptureActive) {
        [menu addAction:[UIAlertAction actionWithTitle:@"Mark lag spike" style:UIAlertActionStyleDefault
            handler:^(__unused UIAlertAction *action) {
                const uint64_t frame = theft4_frame_counter_published_frames();
                const uint64_t now = (uint64_t)(CACurrentMediaTime() * 1e9);
                dispatch_async(self->_publicationCaptureQueue, ^{
                    [self appendPublicationCaptureText:[NSString stringWithFormat:@"marker,%llu,%llu,,user-lag-spike\n",
                        (unsigned long long)frame, (unsigned long long)now]];
                });
            }]];
        [menu addAction:[UIAlertAction actionWithTitle:@"Stop and save capture" style:UIAlertActionStyleDefault
            handler:^(__unused UIAlertAction *action) { [self stopPublicationCapture]; }]];
    }
    [menu addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    menu.popoverPresentationController.sourceView = _frameTimeView;
    menu.popoverPresentationController.sourceRect = _frameTimeView.bounds;
    [self presentViewController:menu animated:YES completion:nil];
}

- (void)refreshFrameRate {
    // Capture stays in preallocated rings during play; save on explicit stop or
    // background only. Five minutes fits16384 records at the30FPS game cap.
    if (_publicationCaptureActive && CACurrentMediaTime() - _publicationCaptureStartTime >= 300)
        [self stopPublicationCapture];
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
    if (_loading || _executionAttempted || !game || !_supportURL || _failure) return;
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
    setenv("THEFT4_PERFORMANCE_CAPTURE", "0", 1);
    if (theft4_configure_boot_diagnostics() != 0) {
        [self bootEvent:@"Cannot configure loader diagnostics"];
        return;
    }
    if (execute) {
        [self record:[@"performance.launch " stringByAppendingString:Theft4PerformanceProfileFields()]];
        rex_frame_scheduling_set_mode(_frameScheduling.on);
        setenv("THEFT4_COMMAND_STREAM", _commandStream.on ? "1" : "0", 1);
        setenv("THEFT4_CPU_CLEANUP", _cpuCleanup.on ? "1" : "0", 1);
        setenv("THEFT4_MEMORY_RECOVERY", _memoryRecovery.on ? "1" : "0", 1);
        setenv("THEFT4_GRAPHICS_PREPARATION", _graphicsPreparation.on ? "1" : "0", 1);
        setenv("THEFT4_FUSED_SMAA", _fusedSmaa.on ? "1" : "0", 1);
        setenv("THEFT4_HARDWARE_SMAA", _hardwareSmaa.on ? "1" : "0", 1);
        setenv("THEFT4_FRAME_RESOURCE_SHARING", _frameResourceSharing.on ? "1" : "0", 1);
        setenv("THEFT4_FRAME_ASSEMBLY", _frameAssembly.on ? "1" : "0", 1);
        setenv("THEFT4_PARALLEL_TEXTURE_CONVERSION", _parallelTextureConversion.on ? "1" : "0", 1);
        setenv("THEFT4_PARALLEL_PREPARATION", _parallelPreparation.on ? "1" : "0", 1);
        setenv("THEFT4_RENDERER_EFFICIENCY", _rendererEfficiency.on ? "1" : "0", 1);
        setenv("THEFT4_PREWARM_TARGET_REUSE", _prewarmTargetReuse.on ? "1" : "0", 1);
        setenv("THEFT4_DIRECT_GUEST_CLOCK", _directGuestClock.on ? "1" : "0", 1);
        setenv("THEFT4_RUNTIME_WAIT_FIXES", _runtimeWaitImprovements.on ? "1" : "0", 1);
        if (_performanceCapture.on) [self beginPublicationCapture];
        // Apply the persisted launcher choice before the background runtime
        // reads and validates its native-renderer launch configuration.
        setenv("THEFT4_ANISOTROPY", _anisotropicFiltering.on ? "4x" : "1x", 1);
        setenv("THEFT4_MOTION_BLUR", _motionBlur.on ? "1" : "0", 1);
        setenv("THEFT4_DEPTH_OF_FIELD", _depthOfField.on ? "1" : "0", 1);
        const char *shadowPresets[] = {"optimized", "original", "enhanced", "ultra"};
        const char *distancePresets[] = {"0.70", "1", "2", "3"};
        const char *reflectionPresets[] = {"original", "1080p", "full"};
        const char *antiAliasingPresets[] = {"off", "fxaa", "smaa"};
        setenv("THEFT4_SHADOW_QUALITY", shadowPresets[_shadowQuality.selectedSegmentIndex], 1);
        setenv("THEFT4_DRAW_DISTANCE", distancePresets[_drawDistance.selectedSegmentIndex], 1);
        setenv("THEFT4_FORCE_HIGHEST_LOD", _modelDetail.selectedSegmentIndex == 2 ? "1" : "0", 1);
        setenv("THEFT4_LOD_SELECTION_BIAS", _modelDetail.selectedSegmentIndex == 0 ? "1.75" : "1", 1);
        setenv("THEFT4_OPTIMIZED_LOCAL_LIGHTS", _drawDistance.selectedSegmentIndex == 0 ? "1" : "0", 1);
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
                strcmp(getenv("THEFT4_DEVICE_PROFILE") ?: "", "a19") == 0 || _limitedMemoryProfile);
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
        _depthOfField.enabled = NO;
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
    if (_publicationCaptureActive) {
        const uint64_t now = (uint64_t)(CACurrentMediaTime() * 1e9);
        dispatch_async(_publicationCaptureQueue, ^{ [self appendPublicationCaptureText:
            [NSString stringWithFormat:@"marker,,%llu,,app-active\n", (unsigned long long)now]]; });
    }
    [_bringupOverlay setActive:!_executionAttempted];
    _sceneActive = YES;
    [self updateFrameTimeHUD];
    _touchControls.active = _gamePresentation && _showControls.on;
    [self createCore];
    if (_core) [self accept:theft4_core_activate(_core) operation:@"activate"];
#ifdef THEFT4_HAS_GTA4_NATIVE_BACKEND
    rex_frame_scheduling_active(1);
    theft4_native_set_active(true);
#endif
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
    [self drainPublicationCapture];
    if (_publicationCaptureActive) {
        const uint64_t now = (uint64_t)(CACurrentMediaTime() * 1e9);
        dispatch_async(_publicationCaptureQueue, ^{ [self appendPublicationCaptureText:
            [NSString stringWithFormat:@"marker,,%llu,,app-inactive\n", (unsigned long long)now]]; });
    }
#ifdef THEFT4_HAS_GTA4_NATIVE_BACKEND
    rex_frame_scheduling_active(0);
    const BOOL quiesced = theft4_native_set_active(false);
    [self record:quiesced ? @"renderer.quiesced" : @"renderer.pause_failed"];
    [self drainPublicationCapture];
#endif
    [_bringupOverlay setActive:NO];
    _sceneActive = NO;
    [self updateFrameTimeHUD];
    _touchControls.active = NO;
    if (_core) [self accept:theft4_core_pause(_core) operation:@"pause"];
    [self refresh];
}
- (void)shutdown {
    rex_frame_scheduling_active(0);
    [self stopPublicationCapture];
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
    rex_gta4_light_memory_warning();
    if (_core) [self accept:theft4_core_memory_warning(_core) operation:@"memory_warning"];
    [self refresh];
}
- (void)dealloc {
    [NSNotificationCenter.defaultCenter removeObserver:self];
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
