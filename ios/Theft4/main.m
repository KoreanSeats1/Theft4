#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <os/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include "theft4_core.h"
#include "theft4_metal_presenter.h"
#ifdef THEFT4_LAB_NATIVE_CAPTURE
extern int rex_gta4_native_profile_start(void);
#endif
#import "Theft4TouchControls.h"
#import "Theft4LauncherView.h"
#import "Theft4FrameTimeView.h"
#ifdef THEFT4_HAS_GAME_LOADER
#include "theft4_boot.h"
#endif

@interface Theft4MetalView : UIView
@end
@implementation Theft4MetalView
+ (Class)layerClass { return CAMetalLayer.class; }
@end

@interface Theft4ViewController : UIViewController {
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
    UISwitch *_showControls;
    UISwitch *_anisotropicFiltering;
    UISwitch *_enhancedOutput;
    UISwitch *_fsrBoost;
    UISwitch *_motionBlur;
    Theft4TouchControls *_touchControls;
    BOOL _gamePresentation;
#ifdef THEFT4_LAB_NATIVE_CAPTURE
    BOOL _nativeProfileRequested;
#endif
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
@end

static void coreEvent(void *context, const char *event) {
    Theft4ViewController *controller = (__bridge Theft4ViewController *)context;
    [controller record:[NSString stringWithUTF8String:event]];
}

static void configureDeviceProfile(void) {
    if (getenv("THEFT4_DEVICE_PROFILE")) return;

    struct utsname systemInfo = {};
    const char *machine = uname(&systemInfo) == 0 ? systemInfo.machine : "unknown";
    // This selects launch defaults only; it does not enable a new instruction set.
    const BOOL isA19 = strncmp(machine, "iPhone18,", 9) == 0;
    const char *profile = isA19 ? "a19" :
        (UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad ? "ipad" : "generic");
    setenv("THEFT4_DEVICE_PROFILE", profile, 0);
    setenv("THEFT4_DEVICE_MODEL", machine, 0);
    os_log(OS_LOG_DEFAULT, "Theft4 launch profile %{public}s for %{public}s", profile, machine);
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
    configureDeviceProfile();
    [NSUserDefaults.standardUserDefaults registerDefaults:@{
        @"Theft4ShowFPS": @YES,
        @"Theft4ShowFrameTime": @NO,
        @"Theft4ShowTouchControls": @NO,
        @"Theft4AnisotropicFiltering": @YES,
        @"Theft4EnhancedOutput1080p": @YES,
        @"Theft4ExperimentalFSRBoost": @NO,
        @"Theft4MotionBlur": @YES
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
#ifndef THEFT4_HAS_GAME_STARTUP
    _start.hidden = YES;
#endif
    _showFrameTime = _bringupOverlay.showFrameTime;
    _showFPS = _bringupOverlay.showFPS; _showControls = _bringupOverlay.showControls;
    _anisotropicFiltering = _bringupOverlay.anisotropicFiltering;
    _enhancedOutput = _bringupOverlay.enhancedOutput; _fsrBoost = _bringupOverlay.fsrBoost;
    _motionBlur = _bringupOverlay.motionBlur;
    NSArray *toggles = @[_showFrameTime,_showFPS,_showControls,_anisotropicFiltering,_enhancedOutput,_fsrBoost,_motionBlur];
    NSArray *keys = @[@"Theft4ShowFrameTime",@"Theft4ShowFPS",@"Theft4ShowTouchControls",@"Theft4AnisotropicFiltering",
                      @"Theft4EnhancedOutput1080p",@"Theft4ExperimentalFSRBoost",@"Theft4MotionBlur"];
    for (NSUInteger i=0;i<toggles.count;++i) {
        UISwitch *toggle = toggles[i];
        toggle.on = [NSUserDefaults.standardUserDefaults boolForKey:keys[i]];
        [toggle addTarget:self action:@selector(displaySettingsChanged:) forControlEvents:UIControlEventValueChanged];
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
#ifdef THEFT4_LAB_NATIVE_CAPTURE
    const char *nativeProfile = getenv("THEFT4_LAB_NATIVE_PROFILE");
    if (nativeProfile && strcmp(nativeProfile, "1") == 0) {
        _fpsLabel.userInteractionEnabled = YES;
        _fpsLabel.accessibilityHint = @"Double-tap to request one frame-timing capture";
        UITapGestureRecognizer *capture = [[UITapGestureRecognizer alloc]
            initWithTarget:self action:@selector(requestNativeProfile)];
        capture.numberOfTapsRequired = 2;
        [_fpsLabel addGestureRecognizer:capture];
    }
#endif
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
    [self.view addSubview:_frameTimeView];
    _frameTimeTop = [_frameTimeView.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor constant:52];
    [NSLayoutConstraint activateConstraints:@[_frameTimeTop,
        [_frameTimeView.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor constant:-10],
        [_frameTimeView.widthAnchor constraintEqualToConstant:244],
        [_frameTimeView.heightAnchor constraintEqualToConstant:92]]];
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
}

- (void)initializeSharedGameDirectory {
    NSError *error = nil;
    NSString *displayName = NSBundle.mainBundle.infoDictionary[@"CFBundleDisplayName"] ?: @"Theft4";
    NSURL *documents = [NSFileManager.defaultManager URLForDirectory:NSDocumentDirectory
        inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:&error];
    NSURL *game = [documents URLByAppendingPathComponent:@"game" isDirectory:YES];
    if (!documents || !game || ![NSFileManager.defaultManager createDirectoryAtURL:game
        withIntermediateDirectories:YES attributes:nil error:&error]) {
        _failure = [NSString stringWithFormat:@"Cannot create the shared game folder: %@",
            error.localizedDescription ?: @"Documents is unavailable"];
        return;
    }

    NSURL *instructionsURL = [documents URLByAppendingPathComponent:@"COPY GAME FILES HERE.txt"];
    if (![NSFileManager.defaultManager fileExistsAtPath:instructionsURL.path]) {
        NSString *instructions = [NSString stringWithFormat:
            @"%@ game-file transfer\n\n"
            @"Open the game folder next to this file and copy the CONTENTS of your prepared "
            @"installation into it. The final layout must include game/default.xex, "
            @"game/default.xexp, and game/update. A raw ISO will not work.\n\n"
            @"Return to %@ and choose Verify Game Files when the transfer finishes.\n",
            displayName, displayName];
        if (![instructions writeToURL:instructionsURL atomically:YES
            encoding:NSUTF8StringEncoding error:&error]) {
            _failure = [NSString stringWithFormat:@"Cannot create transfer instructions: %@",
                error.localizedDescription ?: @"write failed"];
            return;
        }
    }

    [game setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:nil];
    BOOL hasBase = [NSFileManager.defaultManager
        fileExistsAtPath:[[game URLByAppendingPathComponent:@"default.xex"] path]];
    BOOL hasUpdate = [NSFileManager.defaultManager
        fileExistsAtPath:[[game URLByAppendingPathComponent:@"default.xexp"] path]];
    _bootStatus = hasBase && hasUpdate
        ? @"Game files detected. Verify them before starting."
        : [NSString stringWithFormat:@"Transfer folder ready: Files → On My iPhone/iPad → %@ → game", displayName];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    const CGFloat scale = self.view.window.screen.scale ?: UIScreen.mainScreen.scale;
    theft4_metal_resize_layer((__bridge void *)_metalView.layer,
                              _metalView.bounds.size.width,
                              _metalView.bounds.size.height, scale);
}

- (void)displaySettingsChanged:(UISwitch *)sender {
    if (sender == _fsrBoost && _fsrBoost.on) _enhancedOutput.on = YES;
    if (sender == _enhancedOutput && !_enhancedOutput.on) _fsrBoost.on = NO;
    [NSUserDefaults.standardUserDefaults setBool:_fsrBoost.on forKey:@"Theft4ExperimentalFSRBoost"];
    [NSUserDefaults.standardUserDefaults setBool:_motionBlur.on forKey:@"Theft4MotionBlur"];
    [_bringupOverlay refreshConfigurationSummary];
    [NSUserDefaults.standardUserDefaults setBool:_showFrameTime.on forKey:@"Theft4ShowFrameTime"];
    [self updateFrameTimeHUD];
    [NSUserDefaults.standardUserDefaults setBool:_showFPS.on forKey:@"Theft4ShowFPS"];
    [NSUserDefaults.standardUserDefaults setBool:_showControls.on forKey:@"Theft4ShowTouchControls"];
    [NSUserDefaults.standardUserDefaults setBool:_anisotropicFiltering.on
        forKey:@"Theft4AnisotropicFiltering"];
    [NSUserDefaults.standardUserDefaults setBool:_enhancedOutput.on
        forKey:@"Theft4EnhancedOutput1080p"];
    _fpsLabel.hidden = !_gamePresentation || !_showFPS.on;
    _touchControls.active = _gamePresentation && _showControls.on;
    _fpsLastFrames = theft4_frame_counter_published_frames();
    _fpsLastTime = CACurrentMediaTime();
}

- (void)record:(NSString *)event {
    static os_log_t log;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        log = os_log_create(NSBundle.mainBundle.bundleIdentifier.UTF8String, "lifecycle");
    });
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

- (void)refresh {
    theft4_core_snapshot snapshot = {.struct_size = sizeof(snapshot), .abi_version = THEFT4_CORE_ABI_VERSION};
    if (_core && theft4_core_get_snapshot(_core, &snapshot) == THEFT4_OK) {
        NSArray *states = @[@"Core ready", @"Core active", @"Core paused", @"Core stopped"];
        _status.text = _failure ?: (_bootStatus ?: states[snapshot.state]);
        _detail.text = [NSString stringWithFormat:@"Platform  %s\nCore      %s\nC ABI     %u\nPage size %llu bytes\nGame progress is reported above.\n\nLogs: Application Support/Theft4/lifecycle.jsonl",
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
        [_frameTimeTimer invalidate]; _frameTimeTimer = nil;
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
        }];
        [NSRunLoop.mainRunLoop addTimer:_frameTimeTimer forMode:NSRunLoopCommonModes];
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

#ifdef THEFT4_LAB_NATIVE_CAPTURE
- (void)requestNativeProfile {
    if (!_gamePresentation || _nativeProfileRequested) return;
    if (rex_gta4_native_profile_start()) {
        _nativeProfileRequested = YES;
        // Orange means requested; export completion is verified in the log.
        _fpsLabel.backgroundColor = [UIColor colorWithRed:0.65 green:0.28 blue:0.0 alpha:0.9];
        _fpsLabel.accessibilityHint = @"Frame-timing capture requested for this launch";
        [self record:@"lab.native_profile_requested"];
    }
}
#endif

- (BOOL)prefersStatusBarHidden { return _executionAttempted; }
- (BOOL)prefersHomeIndicatorAutoHidden { return _executionAttempted; }

- (void)prepareTransferredGame {
    NSURL *documents = [NSFileManager.defaultManager URLForDirectory:NSDocumentDirectory
        inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:nil];
    [self startGamePreparation:[documents URLByAppendingPathComponent:@"game" isDirectory:YES]];
}

- (void)startGamePreparation:(NSURL *)game {
    [self startGamePreparation:game execute:NO];
}

- (void)startTransferredGame {
    NSURL *documents = [NSFileManager.defaultManager URLForDirectory:NSDocumentDirectory
        inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:nil];
    [self startGamePreparation:[documents URLByAppendingPathComponent:@"game" isDirectory:YES] execute:YES];
}

- (void)startGamePreparation:(NSURL *)game execute:(BOOL)execute {
#ifdef THEFT4_HAS_GAME_LOADER
    if (_loading || _executionAttempted || !game || !_supportURL || _failure) return;
#ifndef THEFT4_HAS_GAME_STARTUP
    if (execute) return;
#endif
    BOOL directory = NO;
    if (![NSFileManager.defaultManager fileExistsAtPath:game.path isDirectory:&directory] || !directory) {
        [self bootEvent:@"Copy the prepared game folder into Theft4 using Finder first."];
        return;
    }
    if (theft4_configure_boot_diagnostics() != 0) {
        [self bootEvent:@"Cannot configure loader diagnostics"];
        return;
    }
    if (execute) {
        // Apply the persisted launcher choice before the background runtime
        // reads and validates its native-renderer launch configuration.
        setenv("THEFT4_ANISOTROPY", _anisotropicFiltering.on ? "4x" : "1x", 1);
        setenv("THEFT4_MOTION_BLUR", _motionBlur.on ? "1" : "0", 1);
        [self.view layoutIfNeeded];
        UIScreen *screen = self.view.window.screen ?: UIScreen.mainScreen;
        CGFloat nativeScale = screen.nativeScale;
        theft4_metal_set_output_mode(
            _fsrBoost.on ? THEFT4_OUTPUT_FSR_BOOST :
                (_enhancedOutput.on ? THEFT4_OUTPUT_FSR_1080P : THEFT4_OUTPUT_720P),
            (uint32_t)floor(_metalView.bounds.size.width * nativeScale),
            (uint32_t)floor(_metalView.bounds.size.height * nativeScale));
        // Release all decorative GPU work before initializing the game device.
        [_bringupOverlay retireScene];
        _fsrBoost.enabled = NO;
        _bringupOverlay.restartButton.enabled = NO;
        _enhancedOutput.enabled = NO;
        _anisotropicFiltering.enabled = NO;
        _motionBlur.enabled = NO;
    }
    NSError *backupError = nil;
    if (![game setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:&backupError])
        [self record:@"boot.cannot_exclude_game_from_backup"];
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
    _sceneActive = YES;
    [self updateFrameTimeHUD];
    [_bringupOverlay setActive:!_executionAttempted];
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
    _sceneActive = NO;
    [self updateFrameTimeHUD];
    [_bringupOverlay setActive:NO];
    _touchControls.active = NO;
    if (_core) [self accept:theft4_core_pause(_core) operation:@"pause"];
    [self refresh];
}
- (void)shutdown {
    _sceneActive = NO;
    [self updateFrameTimeHUD];
    [_bringupOverlay setActive:NO];
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
    [_frameTimeTimer invalidate];
    theft4_frame_time_set_enabled(false);
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
