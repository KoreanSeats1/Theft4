#import <UIKit/UIKit.h>
#import "Theft4LauncherView.h"
#import "Theft4FrameTimeView.h"

@interface PreviewController : UIViewController
@property(nonatomic) Theft4LauncherView *launcher;
@end
@implementation PreviewController
- (void)loadView {
    self.launcher = [Theft4LauncherView new]; self.view = self.launcher;
    self.launcher.enhancedOutput.on = YES; self.launcher.anisotropicFiltering.on = YES;
    self.launcher.showFPS.on = YES;
    self.launcher.motionBlur.on = NO;
    NSArray *args = NSProcessInfo.processInfo.arguments;
    self.launcher.renderResolution.selectedSegmentIndex = [args containsObject:@"--1080p"] ? 3 :
        [args containsObject:@"--900p"] ? 2 : [args containsObject:@"--540p"] ? 0 : 1;
    self.launcher.fsrUpscaling.on = ![args containsObject:@"--no-fsr"];
    self.launcher.statusLabel.text = @"LAUNCHER PREVIEW  /  NO GAME RUNTIME";
    self.launcher.detailLabel.text = @"Isolated simulator UI preview.\nNo game files, saves or runtime are accessed.";
    [self.launcher refreshConfigurationSummary];
    [self.launcher.startButton addTarget:self action:@selector(retire)
        forControlEvents:UIControlEventTouchUpInside];
    NSArray *toggles = @[self.launcher.enhancedOutput,self.launcher.fsrBoost,
        self.launcher.anisotropicFiltering,self.launcher.showFPS,self.launcher.showControls,
        self.launcher.motionBlur];
    for (UISwitch *toggle in toggles)
        [toggle addTarget:self action:@selector(changed:) forControlEvents:UIControlEventValueChanged];
    [self.launcher.renderResolution addTarget:self action:@selector(changed:)
        forControlEvents:UIControlEventValueChanged];
    [self.launcher.fsrUpscaling addTarget:self action:@selector(changed:)
        forControlEvents:UIControlEventValueChanged];
}
- (void)changed:(UIControl *)sender {
    if (sender == self.launcher.fsrBoost && self.launcher.fsrBoost.on) self.launcher.enhancedOutput.on = YES;
    if (sender == self.launcher.enhancedOutput && !self.launcher.enhancedOutput.on) self.launcher.fsrBoost.on = NO;
    [self.launcher refreshConfigurationSummary];
}
- (void)retire {
    [self.launcher retireScene];
    self.launcher.statusLabel.text = @"SCENE RETIRED  /  NO MENU GPU WORK";
}
- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    NSArray *args = NSProcessInfo.processInfo.arguments;
    if ([args containsObject:@"--landscape"]) {
        UIWindowSceneGeometryPreferencesIOS *geometry = [[UIWindowSceneGeometryPreferencesIOS alloc]
            initWithInterfaceOrientations:UIInterfaceOrientationMaskLandscapeRight];
        [self.view.window.windowScene requestGeometryUpdateWithPreferences:geometry errorHandler:nil];
    }
    if ([args containsObject:@"--display"] || [args containsObject:@"--system"]) {
        NSString *identifier = [args containsObject:@"--display"] ? @"launcher.tab.1" : @"launcher.tab.2";
        [self selectInView:self.launcher identifier:identifier];
    }
    if ([args containsObject:@"--retire"]) [self retire];
    if ([args containsObject:@"--frame-time-preview"]) {
        [self.launcher retireScene];
        theft4_frame_time_snapshot snapshot = {0};
        snapshot.count = THEFT4_FRAME_TIME_SAMPLES;
        for (unsigned i = 0; i < snapshot.count; ++i)
            snapshot.milliseconds[i] = i == 105 ? 72 : i == 106 ? 17 : i % 37 == 0 ? 42 : 33.3;
        if ([args containsObject:@"--stall"]) snapshot.pending_ms = 128;
        Theft4FrameTimeView *graph = [Theft4FrameTimeView new];
        graph.translatesAutoresizingMaskIntoConstraints = NO;
        [graph updateWithSnapshot:&snapshot];
        [self.view addSubview:graph];
        BOOL fps = ![args containsObject:@"--no-fps"];
        if (fps) {
            UILabel *counter = [UILabel new]; counter.translatesAutoresizingMaskIntoConstraints = NO;
            counter.text = @"30.0 FPS"; counter.textColor = UIColor.whiteColor;
            counter.textAlignment = NSTextAlignmentCenter;
            counter.font = [UIFont monospacedDigitSystemFontOfSize:15 weight:UIFontWeightSemibold];
            counter.backgroundColor = [UIColor colorWithWhite:0 alpha:0.58];
            counter.layer.cornerRadius = 7; counter.clipsToBounds = YES;
            [self.view addSubview:counter];
            [NSLayoutConstraint activateConstraints:@[
                [counter.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor constant:10],
                [counter.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor constant:-10],
                [counter.widthAnchor constraintEqualToConstant:92],
                [counter.heightAnchor constraintEqualToConstant:32]]];
        }
        [NSLayoutConstraint activateConstraints:@[
            [graph.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor constant:fps ? 52 : 10],
            [graph.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor constant:-10],
            [graph.widthAnchor constraintEqualToConstant:244],
            [graph.heightAnchor constraintEqualToConstant:92]]];
        self.launcher.statusLabel.text = @"GRAPH PREVIEW  /  SYNTHETIC TIMING DATA";
    }
}
- (void)selectInView:(UIView *)view identifier:(NSString *)identifier {
    if ([view.accessibilityIdentifier isEqualToString:identifier] && [view isKindOfClass:UIButton.class])
        [(UIButton *)view sendActionsForControlEvents:UIControlEventTouchUpInside];
    for (UIView *child in view.subviews) [self selectInView:child identifier:identifier];
}
@end
@interface PreviewScene : UIResponder <UIWindowSceneDelegate>
@property(nonatomic) UIWindow *window;
@property(nonatomic) PreviewController *controller;
@end
@implementation PreviewScene
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    self.controller = [PreviewController new];
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    self.window.rootViewController = self.controller; [self.window makeKeyAndVisible];
}
- (void)sceneDidBecomeActive:(UIScene *)scene { [self.controller.launcher setActive:YES]; }
- (void)sceneWillResignActive:(UIScene *)scene { [self.controller.launcher setActive:NO]; }
@end
@interface PreviewApp : UIResponder <UIApplicationDelegate>
@end
@implementation PreviewApp
@end
int main(int argc, char **argv) {
    @autoreleasepool { return UIApplicationMain(argc,argv,nil,NSStringFromClass(PreviewApp.class)); }
}
