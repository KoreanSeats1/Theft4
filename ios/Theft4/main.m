#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <os/log.h>
#include <stdio.h>
#include "theft4_core.h"
#include "theft4_metal_presenter.h"
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
    UIScrollView *_bringupOverlay;
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
@end

static void coreEvent(void *context, const char *event) {
    Theft4ViewController *controller = (__bridge Theft4ViewController *)context;
    [controller record:[NSString stringWithUTF8String:event]];
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
    UILabel *title = [UILabel new];
    title.text = @"Theft4";
    title.font = [UIFont systemFontOfSize:44 weight:UIFontWeightBold];
    title.textColor = UIColor.whiteColor;
    UILabel *subtitle = [UILabel new];
    subtitle.text = @"iOS RUNTIME BRING-UP";
    subtitle.font = [UIFont monospacedSystemFontOfSize:13 weight:UIFontWeightMedium];
    subtitle.textColor = UIColor.systemTealColor;
    _status = [UILabel new];
    _status.font = [UIFont preferredFontForTextStyle:UIFontTextStyleTitle2];
    _status.textColor = UIColor.whiteColor;
    _status.accessibilityIdentifier = @"core.status";
    _detail = [UILabel new];
    _detail.font = [UIFont monospacedSystemFontOfSize:14 weight:UIFontWeightRegular];
    _detail.textColor = UIColor.lightGrayColor;
    _detail.accessibilityIdentifier = @"core.details";
    UILabel *scope = [UILabel new];
    scope.text = @"LibertyRecomp is linked inside this app.\n\nThis probe does not load game files or start the game runtime. Graphics, audio, and guest execution are not enabled.";
#ifdef THEFT4_HAS_GAME_LOADER
    scope.text = @"Loading the real game executable into LibertyRecomp.\n\nThis starts and shuts down the runtime without executing the game. Xbox API exports, graphics, and audio still need integration.";
#endif
#ifdef THEFT4_HAS_GAME_STARTUP
    scope.text = @"Experimental GTA IV startup with real Xbox memory, file, threading, and AOT execution.\n\nNative Metal presentation is connected; Xenos draw commands and audio output are still being integrated. Startup saves are isolated from your existing saves.";
#endif
    scope.font = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
    scope.textColor = UIColor.lightGrayColor;
    UIButton *restart = [UIButton buttonWithType:UIButtonTypeSystem];
    restart.configuration = [UIButtonConfiguration tintedButtonConfiguration];
    [restart setTitle:@"Restart core probe" forState:UIControlStateNormal];
    restart.accessibilityIdentifier = @"core.restart";
    [restart addTarget:self action:@selector(restartCore) forControlEvents:UIControlEventTouchUpInside];
    _prepare = [UIButton buttonWithType:UIButtonTypeSystem];
    _prepare.configuration = [UIButtonConfiguration filledButtonConfiguration];
    [_prepare setTitle:@"Prepare transferred game" forState:UIControlStateNormal];
    [_prepare addTarget:self action:@selector(prepareTransferredGame) forControlEvents:UIControlEventTouchUpInside];
    _start = [UIButton buttonWithType:UIButtonTypeSystem];
    _start.configuration = [UIButtonConfiguration filledButtonConfiguration];
    [_start setTitle:@"Attempt game startup" forState:UIControlStateNormal];
    [_start addTarget:self action:@selector(startTransferredGame) forControlEvents:UIControlEventTouchUpInside];
#ifndef THEFT4_HAS_GAME_STARTUP
    _start.hidden = YES;
#endif
    UILabel *transfer = [UILabel new];
    transfer.text = @"On your Mac: Finder → iPad → Files → Theft4.\nCopy the prepared folder named game, wait for the transfer to finish, then tap below. This checks and loads the executable; it does not start gameplay.";
    transfer.numberOfLines = 0;
    transfer.textColor = UIColor.lightGrayColor;
    transfer.font = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
    NSArray<UILabel *> *labels = @[title, subtitle, _status, _detail, scope];
    for (UILabel *label in labels) { label.numberOfLines = 0; label.adjustsFontForContentSizeCategory = YES; }
    UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:@[title, subtitle, _status, _detail, scope, transfer, _prepare, _start, restart]];
    stack.axis = UILayoutConstraintAxisVertical;
    stack.spacing = 24;
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    _bringupOverlay = [UIScrollView new];
    _bringupOverlay.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_bringupOverlay];
    [_bringupOverlay addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [_bringupOverlay.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor],
        [_bringupOverlay.bottomAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor],
        [_bringupOverlay.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [_bringupOverlay.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [stack.topAnchor constraintEqualToAnchor:_bringupOverlay.contentLayoutGuide.topAnchor constant:32],
        [stack.bottomAnchor constraintEqualToAnchor:_bringupOverlay.contentLayoutGuide.bottomAnchor constant:-32],
        [stack.leadingAnchor constraintEqualToAnchor:_bringupOverlay.contentLayoutGuide.leadingAnchor constant:28],
        [stack.trailingAnchor constraintEqualToAnchor:_bringupOverlay.contentLayoutGuide.trailingAnchor constant:-28],
        [stack.widthAnchor constraintEqualToAnchor:_bringupOverlay.frameLayoutGuide.widthAnchor constant:-56]
    ]];
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
    // Finder File Sharing exposes Documents, not private Application Support.
    [NSFileManager.defaultManager URLForDirectory:NSDocumentDirectory inDomain:NSUserDomainMask
        appropriateForURL:nil create:YES error:nil];
    [self record:@"app.probe_loaded"];
    [self createCore];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    const CGFloat scale = self.view.window.screen.scale ?: UIScreen.mainScreen.scale;
    theft4_metal_resize_layer((__bridge void *)_metalView.layer,
                              _metalView.bounds.size.width,
                              _metalView.bounds.size.height, scale);
}

- (void)record:(NSString *)event {
    static os_log_t log;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ log = os_log_create("com.theft4.bringup", "lifecycle"); });
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
    if (_bringupOverlay.hidden) return;
    [UIView animateWithDuration:0.2 animations:^{
        self->_bringupOverlay.alpha = 0.0;
    } completion:^(BOOL finished) {
        (void)finished;
        self->_bringupOverlay.hidden = YES;
        self->_bringupOverlay.userInteractionEnabled = NO;
    }];
    [self setNeedsStatusBarAppearanceUpdate];
    [self setNeedsUpdateOfHomeIndicatorAutoHidden];
    [self record:@"ui.game_presentation_mode"];
}

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
    if (_core) [self accept:theft4_core_pause(_core) operation:@"pause"];
    [self refresh];
}
- (void)shutdown {
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
