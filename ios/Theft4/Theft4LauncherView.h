#import <UIKit/UIKit.h>

// Presentation only. The app controller owns preferences and the game runtime.
@interface Theft4LauncherView : UIView
@property(nonatomic, readonly) UILabel *statusLabel;
@property(nonatomic, readonly) UILabel *detailLabel;
@property(nonatomic, readonly) UIButton *startButton;
@property(nonatomic, readonly) UIButton *prepareButton;
@property(nonatomic, readonly) UIButton *restartButton;
@property(nonatomic, readonly) UISwitch *showFPS;
@property(nonatomic, readonly) UISwitch *showControls;
@property(nonatomic, readonly) UISwitch *anisotropicFiltering;
@property(nonatomic, readonly) UISwitch *enhancedOutput;
@property(nonatomic, readonly) UISwitch *fsrBoost;
@property(nonatomic, readonly) UISwitch *motionBlur;
- (void)refreshConfigurationSummary;
- (void)setActive:(BOOL)active;
- (void)retireScene;
@end
