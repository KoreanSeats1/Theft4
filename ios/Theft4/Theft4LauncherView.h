#import <UIKit/UIKit.h>

// Presentation only. The app controller owns preferences and the game runtime.
@interface Theft4LauncherView : UIView
@property(nonatomic, readonly) UILabel *statusLabel;
@property(nonatomic, readonly) UILabel *detailLabel;
@property(nonatomic, readonly) UIButton *startButton;
@property(nonatomic, readonly) UIButton *prepareButton;
@property(nonatomic, readonly) UIButton *restartButton;
@property(nonatomic, readonly) UISwitch *showFPS;
@property(nonatomic, readonly) UISwitch *showFrameTime;
@property(nonatomic, readonly) UISwitch *showControls;
@property(nonatomic, readonly) UISwitch *anisotropicFiltering;
@property(nonatomic, readonly) UISwitch *enhancedOutput;
@property(nonatomic, readonly) UISwitch *fsrBoost;
@property(nonatomic, readonly) UISegmentedControl *renderResolution;
@property(nonatomic, readonly) UISwitch *fsrUpscaling;
@property(nonatomic, readonly) uint32_t renderHeight;
@property(nonatomic, readonly) UISwitch *motionBlur;
@property(nonatomic, readonly) UIButton *lowPowerButton;
@property(nonatomic, readonly) UISegmentedControl *shadowQuality;
@property(nonatomic, readonly) UISegmentedControl *drawDistance;
@property(nonatomic, readonly) UISegmentedControl *modelDetail;
@property(nonatomic, readonly) UISegmentedControl *reflectionQuality;
@property(nonatomic, readonly) UISegmentedControl *antiAliasing;
@property(nonatomic, readonly) UISwitch *performanceCapture;
@property(nonatomic, readonly) UIButton *downloadLogButton;
- (void)refreshConfigurationSummary;
- (void)setActive:(BOOL)active;
- (void)retireScene;
@end
