#import <UIKit/UIKit.h>
#import "Theft4LauncherView.h"

@interface PreviewController : UIViewController
@property(nonatomic) Theft4LauncherView *launcher;
@end
@implementation PreviewController
- (void)loadView {
    self.launcher = [Theft4LauncherView new]; self.view = self.launcher;
    self.launcher.enhancedOutput.on = YES; self.launcher.anisotropicFiltering.on = YES;
    self.launcher.showFPS.on = YES;
    self.launcher.motionBlur.on = YES;
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
}
- (void)changed:(UISwitch *)sender {
    if (sender == self.launcher.fsrBoost && sender.on) self.launcher.enhancedOutput.on = YES;
    if (sender == self.launcher.enhancedOutput && !sender.on) self.launcher.fsrBoost.on = NO;
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
