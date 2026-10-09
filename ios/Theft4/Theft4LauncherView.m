#import "Theft4LauncherView.h"
#ifdef THEFT4_BC_TEXTURE_COMPATIBILITY
#import "Theft4TextureSources.h"
#endif
#import "Theft4CityView.h"
#include "theft4_output_policy.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#import <QuartzCore/QuartzCore.h>

#ifdef THEFT4_DIRECT_METAL_DEFAULT
static NSString *const kOptimizedDistanceDescription = @"Optimized: 0.70× world distance. Reduces how far away world objects are drawn and keeps the game's existing LOD transitions.";
static NSString *const kOptimizedDistanceMetrics = @"0.70× world distance";
#else
static NSString *const kOptimizedDistanceDescription = @"Optimized: 0.70× world distance and culls far local light volumes. Distant scenery and some night lighting may change.";
static NSString *const kOptimizedDistanceMetrics = @"0.70× world distance · far local lights reduced";
#endif

static UIColor *Ink(unsigned rgb) {
    return [UIColor colorWithRed:((rgb >> 16) & 255) / 255.0
                           green:((rgb >> 8) & 255) / 255.0
                            blue:(rgb & 255) / 255.0 alpha:1];
}

static UILabel *Copy(NSString *text, CGFloat size, BOOL mono) {
    UILabel *label = [UILabel new];
    label.text = text;
    label.numberOfLines = 0;
    UIFont *font = mono
        ? [UIFont monospacedSystemFontOfSize:size weight:UIFontWeightMedium]
        : [UIFont systemFontOfSize:size weight:UIFontWeightRegular];
    label.font = [[UIFontMetrics metricsForTextStyle:UIFontTextStyleBody] scaledFontForFont:font];
    label.adjustsFontForContentSizeCategory = YES;
    label.textColor = Ink(0xABB7B5);
    return label;
}

static UIStackView *Column(NSArray<UIView *> *views, CGFloat spacing) {
    UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:views];
    stack.axis = UILayoutConstraintAxisVertical;
    stack.spacing = spacing;
    return stack;
}

static UIButton *Action(NSString *title, BOOL primary) {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
    UIButtonConfiguration *config = [UIButtonConfiguration plainButtonConfiguration];
    config.title = title;
    config.cornerStyle = UIButtonConfigurationCornerStyleFixed;
    config.background.cornerRadius = 3;
    config.baseForegroundColor = primary ? Ink(0x111B1D) : Ink(0xE5DECA);
    config.background.backgroundColor = primary ? Ink(0xE6B56B) : Ink(0x243236);
    config.contentInsets = NSDirectionalEdgeInsetsMake(17, 18, 17, 18);
    config.image = [UIImage systemImageNamed:primary ? @"play.fill" : @"arrow.right"];
    config.imagePlacement = NSDirectionalRectEdgeTrailing;
    config.imagePadding = 18;
    button.configuration = config;
    button.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeading;
    [button.heightAnchor constraintGreaterThanOrEqualToConstant:56].active = YES;
    return button;
}

static UISegmentedControl *ChoiceControl(NSArray<NSString *> *items, NSString *identifier,
                                         NSString *label, NSString *hint) {
    UISegmentedControl *control = [[UISegmentedControl alloc] initWithItems:items];
    control.selectedSegmentTintColor = Ink(0xC99755);
    control.backgroundColor = Ink(0x172326);
    const BOOL phone = UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPhone;
    if (phone) {
        for (NSUInteger i = 0; i < control.numberOfSegments; ++i) {
            NSString *title = [control titleForSegmentAtIndex:i];
            if ([title isEqualToString:@"Optimized"]) [control setTitle:@"Opt." forSegmentAtIndex:i];
        }
    }
    NSDictionary *font = phone ? @{NSFontAttributeName:
        [[UIFontMetrics metricsForTextStyle:UIFontTextStyleCaption1] scaledFontForFont:
            [UIFont systemFontOfSize:11 weight:UIFontWeightMedium]]} : @{};
    NSMutableDictionary *normal = [font mutableCopy]; normal[NSForegroundColorAttributeName] = Ink(0xD9D5C7);
    NSMutableDictionary *selected = [font mutableCopy]; selected[NSForegroundColorAttributeName] = Ink(0x10191B);
    [control setTitleTextAttributes:normal
                           forState:UIControlStateNormal];
    [control setTitleTextAttributes:selected
                           forState:UIControlStateSelected];
    control.accessibilityIdentifier = [@"settings." stringByAppendingString:identifier];
    control.accessibilityLabel = label;
    control.accessibilityHint = hint;
    [control.heightAnchor constraintGreaterThanOrEqualToConstant:44].active = YES;
    return control;
}

@implementation Theft4LauncherView {
    Theft4CityView *_city;
    UIView *_atmosphere;
    UIView *_menuPanel;
    CAGradientLayer *_shade;
    CAEmitterLayer *_rain;
    UILabel *_masthead, *_edition, *_wordmark, *_sceneCaption, *_configuration;
    UILabel *_resolutionSummary, *_pageTitle, *_pageDetail, *_controllerHint;
    UILabel *_shadowMetrics, *_distanceMetrics, *_modelMetrics, *_reflectionMetrics, *_aaMetrics, *_sharpeningMetrics;
    UIView *_topRule, *_bottomRule, *_navRule;
    UIScrollView *_scroll, *_navigationScroll;
    UIStackView *_content, *_navigation;
    UIStackView *_play, *_graphics, *_interfacePage, *_system, *_mods;
    UILabel *_playHeadline, *_playIntro, *_playSaveNote;
    NSArray<UIButton *> *_tabs;
    NSArray<UIView *> *_pages;
    BOOL _active, _retired, _portraitMenu, _landscapePhoneMenu;
    NSArray<NSArray<UIView *> *> *_phonePageItems;
    NSInteger _phoneColumns;
}

- (instancetype)initWithFrame:(CGRect)frame {
    if (!(self = [super initWithFrame:frame])) return nil;
    self.backgroundColor = Ink(0x081115);

    _city = [Theft4CityView new];
    [self addSubview:_city];

    _atmosphere = [UIView new];
    _atmosphere.userInteractionEnabled = NO;
    [self addSubview:_atmosphere];
    _shade = [CAGradientLayer layer];
    _shade.colors = @[(id)Ink(0x081115).CGColor,
                      (id)[Ink(0x081115) colorWithAlphaComponent:.94].CGColor,
                      (id)[Ink(0x081115) colorWithAlphaComponent:.18].CGColor,
                      (id)[Ink(0x081115) colorWithAlphaComponent:.02].CGColor];
    _shade.locations = @[@0, @.28, @.68, @1];
    _shade.startPoint = CGPointMake(0, .5);
    _shade.endPoint = CGPointMake(1, .5);
    [_atmosphere.layer addSublayer:_shade];

    UIGraphicsImageRenderer *renderer = [[UIGraphicsImageRenderer alloc]
        initWithSize:CGSizeMake(2, 22)];
    UIImage *drop = [renderer imageWithActions:^(UIGraphicsImageRendererContext *ctx) {
        [[UIColor colorWithWhite:1 alpha:.5] setFill];
        [[UIBezierPath bezierPathWithRoundedRect:CGRectMake(0, 0, 1, 22) cornerRadius:.5] fill];
    }];
    CAEmitterCell *rain = [CAEmitterCell emitterCell];
    rain.contents = (id)drop.CGImage;
    rain.birthRate = 22;
    rain.lifetime = 5;
    rain.velocity = 190;
    rain.velocityRange = 60;
    rain.emissionLongitude = M_PI_2 + .12;
    rain.scale = .7;
    rain.scaleRange = .4;
    rain.color = [Ink(0x99BABC) colorWithAlphaComponent:.12].CGColor;
    _rain = [CAEmitterLayer layer];
    _rain.emitterShape = kCAEmitterLayerLine;
    _rain.emitterCells = @[rain];
    [_atmosphere.layer addSublayer:_rain];

    _masthead = Copy(@"T H E F T 4   /   LIBERTY CITY", 11, YES);
    _masthead.textColor = Ink(0xE5DECA);
    [self addSubview:_masthead];
    BOOL lab = [NSBundle.mainBundle.infoDictionary[@"Theft4LabBuild"] boolValue];
    NSString *version = NSBundle.mainBundle.infoDictionary[@"Theft4ReleaseName"] ?: @"0.3";
    NSString *build = NSBundle.mainBundle.infoDictionary[@"CFBundleVersion"] ?: @"0";
    _edition = Copy([NSString stringWithFormat:@"AFTER HOURS   /   v%@ (%@)", version, build], 11, YES);
    _edition.textAlignment = NSTextAlignmentRight;
    [self addSubview:_edition];
    _topRule = [UIView new];
    _bottomRule = [UIView new];
    _topRule.backgroundColor = _bottomRule.backgroundColor =
        [Ink(0xB7BBA8) colorWithAlphaComponent:.22];
    [self addSubview:_topRule];
    [self addSubview:_bottomRule];

    _menuPanel = [UIView new];
    _menuPanel.backgroundColor = [Ink(0x091316) colorWithAlphaComponent:.88];
    _menuPanel.layer.borderColor = [Ink(0xABB7B5) colorWithAlphaComponent:.15].CGColor;
    _menuPanel.layer.borderWidth = 1;
    _menuPanel.layer.cornerRadius = 4;
    [self addSubview:_menuPanel];

    _wordmark = Copy(@"THEFT4", 67, NO);
    _wordmark.font = [UIFont fontWithName:@"HelveticaNeue-CondensedBlack" size:67]
        ?: [UIFont systemFontOfSize:67 weight:UIFontWeightBlack];
    _wordmark.adjustsFontSizeToFitWidth = YES;
    _wordmark.minimumScaleFactor = .75;
    _wordmark.numberOfLines = 1;
    _wordmark.textColor = Ink(0xECE7D7);
    _wordmark.accessibilityLabel = @"Theft four";
    [_menuPanel addSubview:_wordmark];

    NSMutableArray<UIButton *> *tabs = [NSMutableArray new];
    NSArray<NSString *> *names = @[@"PLAY", @"GRAPHICS", @"INTERFACE", @"SYSTEM", @"MODS"];
    NSArray<NSString *> *symbols = @[@"play.fill", @"slider.horizontal.3", @"rectangle.on.rectangle", @"wrench.and.screwdriver", @"puzzlepiece.extension"];
    for (NSInteger i = 0; i < names.count; ++i) {
        UIButton *tab = [UIButton buttonWithType:UIButtonTypeSystem];
        tab.tag = i;
        UIButtonConfiguration *config = [UIButtonConfiguration plainButtonConfiguration];
        config.title = names[i];
        config.image = [UIImage systemImageNamed:symbols[i]];
        config.imagePadding = 12;
        config.contentInsets = NSDirectionalEdgeInsetsMake(13, 12, 13, 12);
        config.titleTextAttributesTransformer = ^NSDictionary *(NSDictionary *attributes) {
            NSMutableDictionary *updated = [attributes mutableCopy];
            updated[NSFontAttributeName] = [UIFont monospacedSystemFontOfSize:12 weight:UIFontWeightBold];
            return updated;
        };
        tab.configuration = config;
        tab.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeading;
        [tab.heightAnchor constraintGreaterThanOrEqualToConstant:48].active = YES;
        [tab addTarget:self action:@selector(selectTab:) forControlEvents:UIControlEventTouchUpInside];
        tab.accessibilityIdentifier = [@"launcher.tab." stringByAppendingFormat:@"%ld", (long)i];
        [tabs addObject:tab];
    }
    _tabs = tabs;
    _navigation = [[UIStackView alloc] initWithArrangedSubviews:tabs];
    _navigation.axis = UILayoutConstraintAxisVertical;
    _navigation.spacing = 5;
    _navigationScroll = [UIScrollView new];
    _navigationScroll.showsVerticalScrollIndicator = NO;
    [_navigationScroll addSubview:_navigation];
    [_menuPanel addSubview:_navigationScroll];
    _navRule = [UIView new];
    _navRule.backgroundColor = [Ink(0xABB7B5) colorWithAlphaComponent:.14];
    [_menuPanel addSubview:_navRule];

    _pageTitle = Copy(@"PLAY", 12, YES);
    _pageTitle.textColor = Ink(0xE6B56B);
    _pageDetail = Copy(@"CONTINUE INTO LIBERTY CITY", 10, YES);
    [_menuPanel addSubview:_pageTitle];
    [_menuPanel addSubview:_pageDetail];

    _playHeadline = Copy(@"One more night\nin Liberty City.", 31, NO);
    _playHeadline.font = [UIFontMetrics.defaultMetrics scaledFontForFont:
        [UIFont systemFontOfSize:31 weight:UIFontWeightSemibold]];
    _playHeadline.textColor = Ink(0xECE7D7);
    _playIntro = Copy(@"Cross the river. Take the long way home.\nYour next story starts on these streets.", 15, NO);
    _startButton = Action(@"ENTER LIBERTY CITY", YES);
    _startButton.accessibilityIdentifier = @"game.start";
    _playSaveNote = Copy(@"Continue or begin a new story inside the game. Your saves stay with this app.", 12, NO);
    _play = Column(@[_playHeadline, _playIntro, _startButton, _playSaveNote], 22);

    _showCPUUsage = [UISwitch new];
    _showFrameTime = [UISwitch new];
    _showFPS = [UISwitch new];
    _showControls = [UISwitch new];
    _anisotropicFiltering = [UISwitch new];
    _enhancedOutput = [UISwitch new];
    _fsrBoost = [UISwitch new];
    _motionBlur = [UISwitch new];
    _depthOfField = [UISwitch new];
    NSArray<UISwitch *> *switches = @[_showCPUUsage, _showFrameTime, _showFPS, _showControls,
        _anisotropicFiltering, _enhancedOutput, _fsrBoost, _motionBlur, _depthOfField];
    NSArray<NSString *> *identifiers = @[@"showCPUUsage", @"showFrameTime", @"showFPS", @"showTouchControls",
        @"anisotropicFiltering", @"enhancedOutput1080p", @"fsrBoost", @"motionBlur",
        @"depthOfField"];
    for (NSUInteger i = 0; i < switches.count; ++i) {
        switches[i].onTintColor = Ink(0xB6884D);
        switches[i].accessibilityIdentifier = [@"settings." stringByAppendingString:identifiers[i]];
    }

    NSMutableArray<UIView *> *graphicsRows = [NSMutableArray new];
#ifdef THEFT4_DIRECT_METAL_DEFAULT
    _sharpening = [UISlider new];
    _sharpening.minimumValue = 0;
    _sharpening.maximumValue = 100;
    _sharpening.minimumTrackTintColor = Ink(0x35CDD1);
    _sharpening.accessibilityIdentifier = @"settings.sharpening";
    _sharpening.accessibilityLabel = @"Sharpening strength";
    _sharpening.accessibilityHint = @"Zero disables sharpening. Changes apply on the next game launch.";
    [_sharpening.heightAnchor constraintGreaterThanOrEqualToConstant:44].active = YES;
    [_sharpening addTarget:self action:@selector(refreshConfigurationSummary)
        forControlEvents:UIControlEventValueChanged];
    _sharpeningMetrics = Copy(@"OFF · 0%", 12, YES);
#endif
    if (lab) {
        _renderResolution = ChoiceControl(@[@"540p", @"720p", @"900p", @"1080p", @"Native"], @"renderResolution",
            @"Render resolution", @"The internal scene resolution. Applies at the next game launch.");
        _renderResolution.selectedSegmentIndex = 1;
        _fsrUpscaling = [UISwitch new];
        _fsrUpscaling.onTintColor = Ink(0xB6884D);
        _fsrUpscaling.accessibilityIdentifier = @"settings.fsrUpscaling";
        _resolutionSummary = Copy(@"", 12, YES);
        _resolutionSummary.accessibilityIdentifier = @"settings.resolutionSummary";
        [graphicsRows addObjectsFromArray:@[
            [self choice:@"INTERNAL RESOLUTION" detail:@"By default, choices use centered 16:9. The Native Aspect Ratio mod expands the scene to your screen shape. Native uses physical pixels and disables FSR." control:_renderResolution],
            [self setting:@"FSR UPSCALING" detail:
                (strcmp(getenv("THEFT4_DEVICE_PROFILE") ?: "", "a19") == 0 ||
                 strcmp(getenv("THEFT4_DEVICE_PROFILE") ?: "", "iphone-6gb") == 0 ||
                 strcmp(getenv("THEFT4_DEVICE_PROFILE") ?: "", "legacy-ipad") == 0
                    ? @"This device uses a fixed 1080p output budget. Lower internal resolutions use FSR 1; changes apply on the next game launch."
                    : @"Independent of internal resolution. Off bypasses FSR 1 while keeping the selected scene resolution.")
                toggle:_fsrUpscaling],
            _resolutionSummary
        ]];
        _frameSpeedButton = Action(@"FRAME SPEED", NO);
        _frameSpeedButton.accessibilityIdentifier = @"settings.frameSpeed";
        _restoreGraphicsButton = Action(@"RESTORE PREVIOUS SETTINGS", NO);
        _restoreGraphicsButton.accessibilityIdentifier = @"settings.restorePreviousGraphics";
        [graphicsRows addObjectsFromArray:@[_frameSpeedButton,_restoreGraphicsButton,
            Copy(@"Frame Speed keeps your resolution and uses original shadows, earlier resident mesh LOD and shorter draw distance, with edge filters, depth of field and motion blur off. Your selection is kept after reopening.",12,NO)]];
        _lowPowerButton = Action(@"AUTO OPTIMIZE", NO);
        _lowPowerButton.accessibilityIdentifier = @"settings.autoOptimize";
        _lowPowerButton.accessibilityLabel = @"Auto Optimize for this device";
        _originalPresetButton = Action(@"ORIGINAL · XBOX 360", NO);
        _originalPresetButton.accessibilityIdentifier = @"settings.originalPreset";
        _originalPresetButton.accessibilityLabel = @"Original Xbox 360 settings";
        for (UIButton *button in @[_lowPowerButton, _originalPresetButton]) {
            UIButtonConfiguration *config = button.configuration;
            config.image = nil;
            config.contentInsets = NSDirectionalEdgeInsetsMake(12, 6, 12, 6);
            config.titleTextAttributesTransformer = ^NSDictionary *(NSDictionary *attributes) {
                NSMutableDictionary *updated = [attributes mutableCopy];
                updated[NSFontAttributeName] = [UIFont systemFontOfSize:12 weight:UIFontWeightSemibold];
                return updated;
            };
            button.configuration = config;
            button.contentHorizontalAlignment = UIControlContentHorizontalAlignmentCenter;
        }
        UIStackView *presetButtons = [[UIStackView alloc] initWithArrangedSubviews:
            @[_lowPowerButton, _originalPresetButton]];
        presetButtons.axis = UILayoutConstraintAxisHorizontal;
        presetButtons.distribution = UIStackViewDistributionFillEqually;
        presetButtons.spacing = 10;
        [graphicsRows addObjectsFromArray:@[
            presetButtons,
            Copy(@"Auto Optimize selects a starting profile for this device. Original restores 720p, original shadow/distance/detail/reflections, and the title's blur. You can adjust each setting afterward.", 12, NO)
        ]];
    }

    _shadowQuality = ChoiceControl(@[@"Optimized", @"Original", @"Enhanced", @"Ultra"], @"shadowQuality",
        @"Dynamic shadows", @"Optimized is pending validation of distant shadow cache reuse.");
    [_shadowQuality setEnabled:NO forSegmentAtIndex:0];
    _drawDistance = ChoiceControl(@[@"Optimized", @"Original", @"2×", @"3×"], @"drawDistance",
        @"Draw distance", @"Optimized reduces world distance and distant local illumination.");
    _modelDetail = ChoiceControl(@[@"Lower", @"Original", @"Highest"], @"modelDetail",
        @"Model detail", @"Lower selects simpler resident meshes sooner without changing draw distance.");
    _reflectionQuality = ChoiceControl(@[@"Original", @"1080p", @"Full"], @"reflectionQuality",
        @"Reflection resolution", @"Set the renderer's native reflection target preset.");
    _antiAliasing = ChoiceControl(@[@"Off", @"FXAA", @"SMAA"], @"antiAliasing",
        @"Anti-aliasing", @"Select the native renderer's edge smoothing mode.");

    _shadowMetrics = Copy(@"", 12, YES);
    _distanceMetrics = Copy(@"", 12, YES);
    _modelMetrics = Copy(@"", 12, YES);
    _reflectionMetrics = Copy(@"", 12, YES);
    _aaMetrics = Copy(@"", 12, YES);
    for (UILabel *metric in @[_shadowMetrics, _distanceMetrics, _modelMetrics,
                              _reflectionMetrics, _aaMetrics]) {
        metric.textColor = Ink(0xE6B56B);
    }
    for (UISegmentedControl *choice in @[_shadowQuality, _drawDistance, _modelDetail,
                                         _reflectionQuality, _antiAliasing]) {
        [choice addTarget:self action:@selector(refreshConfigurationSummary)
            forControlEvents:UIControlEventValueChanged];
    }

    [graphicsRows addObjectsFromArray:@[
        [self choice:@"DYNAMIC SHADOWS" detail:@"Optimized is locked until distant shadow cache reuse is verified. Original: 256 / 2048². Enhanced: 512 / 4096². Ultra: 1024 / up to 8192²." control:_shadowQuality metrics:_shadowMetrics],
        [self choice:@"DRAW DISTANCE" detail:kOptimizedDistanceDescription control:_drawDistance metrics:_distanceMetrics],
        [self choice:@"MODEL DETAIL" detail:@"Lower selects simpler resident meshes sooner; Highest prefers the best resident mesh. Neither forces missing models to load. Lower may reduce geometry cost, but CPU gains depend on submesh and draw-call counts." control:_modelDetail metrics:_modelMetrics],
        [self choice:@"REFLECTION QUALITY" detail:@"Mirror and water targets / environment cubemap. Full is capped at 1440p in this build." control:_reflectionQuality metrics:_reflectionMetrics],
        [self choice:@"ANTI-ALIASING" detail:@"Edge smoothing after scene rendering; this does not change internal resolution." control:_antiAliasing metrics:_aaMetrics],
        [self setting:@"TEXTURE FILTERING · 4×" detail:@"Cleaner roads and surfaces at an angle." toggle:_anisotropicFiltering],
        [self setting:@"MOTION BLUR" detail:@"Original movement blur. Disable for a sharper image in motion." toggle:_motionBlur],
        [self setting:@"DEPTH OF FIELD" detail:@"Distance-based focus blur. Off by default on iPhone Air; this may sharpen city views, but performance gains need testing." toggle:_depthOfField],
        Copy(@"Graphics changes apply on the next game launch. Extended distance and Ultra shadows can reduce frame rate in dense areas.", 12, NO)
    ]];
#ifdef THEFT4_DIRECT_METAL_DEFAULT
    [graphicsRows insertObject:Column(@[Copy(@"SHARPENING", 12, YES),
        Copy(@"Adjust fine-detail contrast from 0–100%. Zero keeps the original image. Higher values add GPU work and can emphasize texture noise. Applies on the next game launch.", 12, NO),
        _sharpening, _sharpeningMetrics], 8) atIndex:(lab ? 3 : 0)];
#endif
    if (!lab) {
        [graphicsRows insertObjects:@[
            [self setting:@"1080p ENHANCED OUTPUT" detail:@"FSR 1 upscale plus sharpening." toggle:_enhancedOutput],
            [self setting:@"EXPERIMENTAL FSR BOOST" detail:@"Native-pixel 16:9 output while retaining the 720p scene." toggle:_fsrBoost]
        ] atIndexes:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, 2)]];
    }
    _graphics = Column(graphicsRows, 20);

    _interfacePage = Column(@[
        [self setting:@"CPU / THREAD GRAPH" detail:@"Compact device-core and game-thread activity. Updates once per second; included in long captures while enabled." toggle:_showCPUUsage],
        [self setting:@"FRAME COUNTER" detail:@"Unique game frames in the top-right corner." toggle:_showFPS],
        [self setting:@"FRAME-TIME GRAPH" detail:@"Frame delivery against the 33.3 ms target. Double-tap for a short detailed profile. Hold to mark a lag spike or stop and save a long capture." toggle:_showFrameTime],
        [self setting:@"TOUCH CONTROLS" detail:@"Physical controllers continue to work when the overlay is hidden." toggle:_showControls],
        Copy(@"A connected controller can move focus through this launcher. Use the D-pad or left stick to navigate and A to select.", 12, NO)
    ], 20);

    _prepareButton = Action(@"VERIFY GAME FILES", NO);
    _restartButton = Action(@"RESTART CORE PROBE", NO);
    _restartButton.accessibilityIdentifier = @"core.restart";
#ifdef THEFT4_BC_TEXTURE_COMPATIBILITY
    _astcConversion = [UISwitch new];
    _astcConversion.onTintColor = Ink(0x35CDD1);
    _astcConversion.accessibilityIdentifier = @"settings.astcConversion";
#endif
    _skipIntro = [UISwitch new];
    _skipIntro.onTintColor = Ink(0x35CDD1);
    _skipIntro.accessibilityIdentifier = @"settings.skipIntro";
    _performanceCapture = [UISwitch new];
    _performanceCapture.onTintColor = Ink(0xB6884D);
    _performanceCapture.accessibilityIdentifier = @"settings.performanceCapture";
    _downloadLogButton = Action(@"DOWNLOAD LATEST LOG CAPTURE", NO);
    _downloadLogButton.accessibilityIdentifier = @"diagnostics.downloadLatestCapture";
    _exportSavesButton = Action(@"EXPORT SAVES TO FILES", NO);
    _exportSavesButton.accessibilityIdentifier = @"saves.export";
    _importSavesButton = Action(@"IMPORT SAVES FROM FILES", NO);
    _importSavesButton.accessibilityIdentifier = @"saves.import";
    _detailLabel = Copy(@"Waiting for runtime information…", 12, YES);
    _detailLabel.accessibilityIdentifier = @"core.details";
    NSString *displayName = NSBundle.mainBundle.infoDictionary[@"CFBundleDisplayName"] ?: @"Theft4";
    NSMutableArray<UIView *> *systemRows = [NSMutableArray arrayWithArray:@[
        Copy(@"RUNTIME", 13, YES),
        Copy(@"Native ARM64 game code. Your game files. Your city.", 17, NO),
        [self setting:@"SKIP INTRO VIDEOS" detail:@"Skip opening credits and Rockstar logos at the next launch. Game loading still completes normally." toggle:_skipIntro],
        Copy(@"Optimized for normal play. Development logging and probes stay off. Optional graphs and performance captures are available in the launcher.", 12, NO),
        [self setting:@"LONG PERFORMANCE CAPTURE" detail:@"Off each time the app opens. Enable before Play for up to 5 minutes of timing, without development probes. Turn on the frame-time graph to mark a spike or stop and save; capture also saves when the app goes into the background. Data stays in memory during play." toggle:_performanceCapture],
        _downloadLogButton,
        Copy(@"SAVE TRANSFER", 13, YES),
        Copy(@"Export a dated backup to Files → On My iPhone/iPad → Theft4 → Save Exports. Import a Theft4 save-export folder only while the game is closed; current saves are backed up first.", 12, NO),
        _exportSavesButton, _importSavesButton,
        _prepareButton, _restartButton, _detailLabel,
        Copy([NSString stringWithFormat:@"On first launch, %@ creates Files → On My iPhone/iPad → %@ → game. Copy the contents of the prepared game folder into game, then verify.", displayName, displayName], 13, NO)
    ]];
#ifdef THEFT4_BC_TEXTURE_COMPATIBILITY
    UIStackView *astcRow = (UIStackView *)[self setting:@"ASTC TEXTURE COMPATIBILITY"
        detail:@"Automatic one-time setup for this GPU. Saves compressed ASTC copies before Play to reduce texture-conversion pauses and GPU memory use. Later launches reuse them. Devices with direct BC support, such as iPhone 15 Pro (A17 Pro), iPhone 16 (A18), and M3/M4/M5 iPads, skip this requirement."
        toggle:_astcConversion];
    astcRow.backgroundColor = [Ink(0x35CDD1) colorWithAlphaComponent:.12];
    astcRow.layer.borderColor = [Ink(0x35CDD1) colorWithAlphaComponent:.60].CGColor;
    astcRow.layer.borderWidth = 1;
    astcRow.layer.cornerRadius = 8;
    astcRow.layoutMargins = UIEdgeInsetsMake(13, 13, 13, 13);
    astcRow.layoutMarginsRelativeArrangement = YES;
    UIStackView *astcText = (UIStackView *)astcRow.arrangedSubviews.firstObject;
    UILabel *astcTitle = (UILabel *)astcText.arrangedSubviews.firstObject;
    astcTitle.textColor = Ink(0x6BE5E7);
    _deleteTextureCacheButton = Action(@"DELETE PREPARED TEXTURE CACHE", NO);
    _deleteTextureCacheButton.accessibilityIdentifier = @"settings.deleteTextureCache";
    BOOL showTextureCompatibility = Theft4DeviceNeedsBCTexturePreparation();
#ifdef THEFT4_ASTC_EXPERIMENT
    showTextureCompatibility = YES;
#endif
    if (showTextureCompatibility) {
        [systemRows insertObject:astcRow atIndex:2];
        [systemRows insertObject:Column(@[_deleteTextureCacheButton,
            Copy(@"Free the storage used by prepared textures. Confirmation required. Your game files and saves are kept. Devices that need ASTC must prepare textures again before Play.", 12, NO)], 7) atIndex:3];
    }
#endif
    _system = Column(systemRows, 20);

    _customTimeCycle = [UISwitch new];
    _customTimeCycle.onTintColor = Ink(0x35CDD1);
    _customTimeCycle.accessibilityIdentifier = @"mods.customTimeCycle";
    NSString *previewPath = [NSBundle.mainBundle pathForResource:@"preview" ofType:@"jpg"
        inDirectory:@"Mods/CustomTimeCycle"];
    UIImageView *preview = [[UIImageView alloc] initWithImage:
        previewPath ? [UIImage imageWithContentsOfFile:previewPath] : nil];
    preview.contentMode = UIViewContentModeScaleAspectFit;
    preview.backgroundColor = UIColor.blackColor;
    preview.layer.cornerRadius = 4;
    preview.clipsToBounds = YES;
    preview.isAccessibilityElement = YES;
    preview.accessibilityLabel = @"Custom Time Cycle example: a bright Liberty City street with a clear blue sky.";
    preview.accessibilityIdentifier = @"mods.customTimeCycle.preview";
    [preview.heightAnchor constraintEqualToAnchor:preview.widthAnchor multiplier:0.75].active = YES;
    NSString *metadataPath = [NSBundle.mainBundle pathForResource:@"metadata" ofType:@"plist"
        inDirectory:@"Mods/CustomTimeCycle"];
    NSDictionary *metadata = metadataPath ? [NSDictionary dictionaryWithContentsOfFile:metadataPath] : nil;
    NSString *developer = [metadata[@"Developer"] isKindOfClass:NSString.class]
        ? [metadata[@"Developer"] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet] : nil;
    UILabel *credit = Copy(developer.length
        ? [NSString stringWithFormat:@"%@\nThank you for the custom time cycle!", developer]
        : @"Developer credit coming soon.", 13, NO);
    credit.accessibilityIdentifier = @"mods.customTimeCycle.developerCredit";
    _nativeAspect = [UISwitch new];
    _nativeAspect.onTintColor = Ink(0x35CDD1);
    _nativeAspect.accessibilityIdentifier = @"mods.nativeAspect";
    [_nativeAspect addTarget:self action:@selector(refreshConfigurationSummary)
        forControlEvents:UIControlEventValueChanged];
    _godMode = [UISwitch new];
    _godMode.onTintColor = Ink(0x35CDD1);
    _godMode.accessibilityIdentifier = @"mods.godMode";
    _unlimitedAmmo = [UISwitch new];
    _unlimitedAmmo.onTintColor = Ink(0x35CDD1);
    _unlimitedAmmo.accessibilityIdentifier = @"mods.unlimitedAmmo";
    _mods = Column(@[
        Copy(@"Make Liberty City your own.", 22, NO),
        [self setting:@"CUSTOM TIME CYCLE"
            detail:@"Use the bundled time-cycle replacement for lighting, sky, fog and weather appearance. Off uses your original game file."
            toggle:_customTimeCycle],
        preview,
        Copy(@"EXAMPLE SCREENSHOT", 10, YES),
        Copy(@"Actual appearance varies with the time of day, weather and your graphics settings. This mod changes atmosphere settings; it does not replace textures.", 12, NO),
        Column(@[Copy(@"MOD DEVELOPER", 10, YES), credit], 5),
        [self setting:@"NATIVE ASPECT RATIO"
            detail:@"Expand the game view to your screen: more scenery above and below on iPad, or at the sides on wider iPhones. Preserves proportions without cropping or stretching."
            toggle:_nativeAspect],
        Copy(@"Camera projection and visibility expand with the viewport. HUD and phone keep their proportions and respect screen safe areas. Extra scenery and pixels can increase rendering cost. Off restores original 16:9 framing.", 12, NO),
        Copy(@"The view follows the window shape when you press Play. If you rotate or resize afterward, it keeps that shape; reopen to fill the new window. Videos and fixed artwork keep their original proportions.", 12, NO),
        [self setting:@"GOD MODE"
            detail:@"Protect your player from damage and death, including bullets, explosions, fire, collisions and drowning. Other characters remain vulnerable."
            toggle:_godMode],
        [self setting:@"UNLIMITED AMMO"
            detail:@"Keep reserve ammunition for weapons you own. Magazines still empty, and you still reload normally. This does not grant weapons."
            toggle:_unlimitedAmmo],
        Copy(@"Reserve ammo is replenished while enabled. Replenished ammo amounts can be included in your game save; turning the mod off restores normal ammo consumption.", 12, NO),
        Copy(@"Applies when you press Play in a fresh app session. To change it after playing, save, close Theft4 and reopen. Your original game files and saves are kept.", 12, NO)
    ], 16);

    _pages = @[_play, _graphics, _interfacePage, _system, _mods];
    if (UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPhone) {
        // A compact mod gallery replaces the full-width, oversized screenshot.
        NSLayoutConstraint *aspect = nil;
        for (NSLayoutConstraint *c in preview.constraints)
            if (c.firstAttribute == NSLayoutAttributeHeight && c.secondItem == preview) aspect = c;
        aspect.active = NO;
        [preview.widthAnchor constraintEqualToConstant:132].active = YES;
        [preview.heightAnchor constraintEqualToConstant:99].active = YES;
        UIStackView *example = [[UIStackView alloc] initWithArrangedSubviews:@[
            preview, Column(@[Copy(@"LIGHTING PREVIEW", 10, YES),
                Copy(@"Appearance varies with weather and time of day.", 12, NO),
                Copy(@"MOD DEVELOPER", 10, YES), credit], 5)]];
        example.spacing = 12; example.alignment = UIStackViewAlignmentCenter;
        for (UIView *v in _mods.arrangedSubviews.copy) {
            [_mods removeArrangedSubview:v]; [v removeFromSuperview];
        }
        [_mods addArrangedSubview:Column(@[
            [self setting:@"CUSTOM TIME CYCLE"
                detail:@"Bundled lighting, sky, fog and weather replacement. Off uses the original game file."
                toggle:_customTimeCycle], example], 12)];
        [_mods addArrangedSubview:Column(@[
            [self setting:@"NATIVE ASPECT RATIO"
                detail:@"Fill your screen with extra scenery, without stretching or cropping."
                toggle:_nativeAspect],
            Copy(@"HUD keeps its proportions. Extra scenery can cost performance. Videos keep their original shape; reopen after rotating or resizing.", 12, NO)], 8)];
        [_mods addArrangedSubview:[self setting:@"GOD MODE"
            detail:@"No player damage or death. Other characters remain vulnerable." toggle:_godMode]];
        [_mods addArrangedSubview:[self setting:@"UNLIMITED AMMO"
            detail:@"Unlimited reserve ammo. Magazines still empty and reload normally. No weapons granted."
            toggle:_unlimitedAmmo]];
        [_mods addArrangedSubview:Copy(@"Replenished ammo can be saved. Turning the mod off restores normal consumption.", 12, NO)];
        [_mods addArrangedSubview:Copy(@"Choose before Play. Save and reopen the app to change mods after playing.", 12, NO)];
        NSMutableArray *pages = [NSMutableArray new];
        for (UIStackView *page in @[_graphics, _interfacePage, _system, _mods]) {
            NSMutableArray *items = [NSMutableArray new];
            for (UIView *view in page.arrangedSubviews.copy) {
                [page removeArrangedSubview:view]; [view removeFromSuperview];
                if ([view isKindOfClass:UILabel.class]) {
                    [items addObject:view]; continue;
                }
                UIView *card = [UIView new];
                card.backgroundColor = [Ink(0x172326) colorWithAlphaComponent:.94];
                card.layer.cornerRadius = 12;
                card.layer.borderWidth = 0.5;
                card.layer.borderColor = [Ink(0xABB7B5) colorWithAlphaComponent:.16].CGColor;
                UIView *spacer = [UIView new];
                [spacer.heightAnchor constraintGreaterThanOrEqualToConstant:0].active = YES;
                UIStackView *body = Column(@[view, spacer], 0);
                body.translatesAutoresizingMaskIntoConstraints = NO;
                [card addSubview:body];
                [NSLayoutConstraint activateConstraints:@[
                    [body.topAnchor constraintEqualToAnchor:card.topAnchor constant:12],
                    [body.bottomAnchor constraintEqualToAnchor:card.bottomAnchor constant:-12],
                    [body.leadingAnchor constraintEqualToAnchor:card.leadingAnchor constant:12],
                    [body.trailingAnchor constraintEqualToAnchor:card.trailingAnchor constant:-12]
                ]];
                [items addObject:card];
            }
            page.spacing = 12; [pages addObject:items];
        }
        _phonePageItems = pages;
    }
    _scroll = [UIScrollView new];
    _scroll.showsVerticalScrollIndicator = YES;
    _scroll.indicatorStyle = UIScrollViewIndicatorStyleWhite;
    _scroll.alwaysBounceVertical = NO;
    [_menuPanel addSubview:_scroll];
    _content = Column(_pages, 16);
    _content.translatesAutoresizingMaskIntoConstraints = NO;
    [_scroll addSubview:_content];
    [NSLayoutConstraint activateConstraints:@[
        [_content.topAnchor constraintEqualToAnchor:_scroll.contentLayoutGuide.topAnchor],
        [_content.bottomAnchor constraintEqualToAnchor:_scroll.contentLayoutGuide.bottomAnchor constant:-24],
        [_content.leadingAnchor constraintEqualToAnchor:_scroll.contentLayoutGuide.leadingAnchor],
        [_content.trailingAnchor constraintEqualToAnchor:_scroll.contentLayoutGuide.trailingAnchor constant:-8],
        [_content.widthAnchor constraintEqualToAnchor:_scroll.frameLayoutGuide.widthAnchor constant:-8]
    ]];

    _controllerHint = Copy(@"D-PAD  NAVIGATE     A  SELECT", 10, YES);
    _controllerHint.textColor = Ink(0xE5DECA);
    [_menuPanel addSubview:_controllerHint];
    _statusLabel = Copy(@"Core ready", 12, YES);
    _statusLabel.accessibilityIdentifier = @"core.status";
    _statusLabel.numberOfLines = 2;
    [self addSubview:_statusLabel];
    _configuration = Copy(@"", 10, YES);
    _configuration.textAlignment = NSTextAlignmentRight;
    [self addSubview:_configuration];
    _sceneCaption = Copy(@"LIBERTY CITY, AFTER DARK\nDRAG TO ORBIT · PINCH TO ZOOM", 10, YES);
    _sceneCaption.textAlignment = NSTextAlignmentRight;
    [self addSubview:_sceneCaption];

    [self selectTab:_tabs[0]];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(motionChanged:)
        name:UIAccessibilityReduceMotionStatusDidChangeNotification object:nil];
    [self setActive:YES];
    return self;
}

- (UIView *)setting:(NSString *)title detail:(NSString *)detail toggle:(UISwitch *)toggle {
    UILabel *label = Copy(title, 14, YES);
    label.textColor = Ink(0xECE7D7);
    UIStackView *text = Column(@[label, Copy(detail, 12, NO)], 5);
    UIStackView *row = [[UIStackView alloc] initWithArrangedSubviews:@[text, toggle]];
    row.alignment = UIStackViewAlignmentCenter;
    row.spacing = 16;
    toggle.accessibilityLabel = title;
    toggle.accessibilityHint = detail;
    [toggle setContentHuggingPriority:UILayoutPriorityRequired forAxis:UILayoutConstraintAxisHorizontal];
    [toggle setContentCompressionResistancePriority:UILayoutPriorityRequired forAxis:UILayoutConstraintAxisHorizontal];
    return row;
}

- (UIView *)choice:(NSString *)title detail:(NSString *)detail control:(UISegmentedControl *)control {
    UILabel *label = Copy(title, 14, YES);
    label.textColor = Ink(0xECE7D7);
    return Column(@[label, control, Copy(detail, 12, NO)], 7);
}

- (UIView *)choice:(NSString *)title detail:(NSString *)detail
           control:(UISegmentedControl *)control metrics:(UILabel *)metrics {
    UILabel *label = Copy(title, 14, YES);
    label.textColor = Ink(0xECE7D7);
    return Column(@[label, control, metrics, Copy(detail, 12, NO)], 7);
}

- (void)updateTabAppearance {
    for (UIButton *tab in _tabs) {
        BOOL selected = !_pages[tab.tag].hidden;
        BOOL focused = tab.isFocused;
        UIButtonConfiguration *config = tab.configuration;
        config.baseForegroundColor = selected || focused ? Ink(0xF0E4CC) : Ink(0x819091);
        config.background.backgroundColor = focused
            ? [Ink(0xE6B56B) colorWithAlphaComponent:.30]
            : (selected ? [Ink(0xE6B56B) colorWithAlphaComponent:.13] : UIColor.clearColor);
        config.background.strokeColor = focused ? Ink(0xE6B56B) : UIColor.clearColor;
        config.background.strokeWidth = focused ? 1.5 : 0;
        tab.configuration = config;
        tab.accessibilityTraits = UIAccessibilityTraitButton |
            (selected ? UIAccessibilityTraitSelected : 0);
    }
}

- (void)selectTab:(UIButton *)sender {
    NSArray<NSString *> *titles = @[@"PLAY", @"GRAPHICS", @"INTERFACE", @"SYSTEM", @"MODS"];
    NSArray<NSString *> *details = @[@"CONTINUE INTO LIBERTY CITY", @"IMAGE AND WORLD QUALITY",
        @"HUD AND INPUT", @"GAME FILES AND RUNTIME", @"MAKE THE CITY YOUR OWN"];
    for (NSUInteger i = 0; i < _pages.count; ++i) _pages[i].hidden = i != sender.tag;
    _pageTitle.text = titles[sender.tag];
    _pageDetail.text = details[sender.tag];
    [self updateTabAppearance];
    [_navigationScroll scrollRectToVisible:sender.frame animated:NO];
    [_scroll setContentOffset:CGPointZero animated:NO];
}

- (void)didUpdateFocusInContext:(UIFocusUpdateContext *)context
       withAnimationCoordinator:(UIFocusAnimationCoordinator *)coordinator {
    [super didUpdateFocusInContext:context withAnimationCoordinator:coordinator];
    [coordinator addCoordinatedAnimations:^{ [self updateTabAppearance]; } completion:nil];
}

- (NSArray<id<UIFocusEnvironment>> *)preferredFocusEnvironments {
    return _startButton.enabled ? @[_startButton] : @[_tabs[0]];
}

- (void)layoutPhonePages:(NSInteger)columns {
    if (!_phonePageItems || _phoneColumns == columns) return;
    _phoneColumns = columns;
    NSArray *pages = @[_graphics, _interfacePage, _system, _mods];
    for (NSUInteger i = 0; i < pages.count; ++i) {
        UIStackView *page = pages[i];
        for (UIView *v in page.arrangedSubviews.copy) {
            [page removeArrangedSubview:v]; [v removeFromSuperview];
        }
        NSArray *items = _phonePageItems[i];
        for (NSUInteger j = 0; j < items.count; ++j) {
            UIView *first = items[j];
            [first removeFromSuperview];
            if (columns == 2 && ![first isKindOfClass:UILabel.class] &&
                j + 1 < items.count && ![items[j + 1] isKindOfClass:UILabel.class]) {
                UIView *second = items[++j]; [second removeFromSuperview];
                UIStackView *row = [[UIStackView alloc] initWithArrangedSubviews:@[first, second]];
                row.spacing = 12; row.distribution = UIStackViewDistributionFillEqually;
                row.alignment = UIStackViewAlignmentFill;
                [page addArrangedSubview:row];
            } else [page addArrangedSubview:first];
        }
    }
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGFloat w = self.bounds.size.width, h = self.bounds.size.height;
    BOOL phone = UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPhone;
    BOOL portraitMenu = phone && h > w;
    BOOL landscapePhoneMenu = phone && w >= h;
    CGFloat inset = landscapePhoneMenu
        ? MAX(20, MAX(self.safeAreaInsets.left, self.safeAreaInsets.right) + 8)
        : (w < 650 ? 22 : 42);
    CGFloat top = MAX(24, self.safeAreaInsets.top + 12);
    CGFloat bottom = MAX(20, self.safeAreaInsets.bottom + 10);
    BOOL compact = w < 850 || phone;
    if (phone) [self layoutPhonePages:landscapePhoneMenu && w - 2 * inset >= 680 &&
        !UIContentSizeCategoryIsAccessibilityCategory(self.traitCollection.preferredContentSizeCategory) ? 2 : 1];

    if (_portraitMenu != portraitMenu || _landscapePhoneMenu != landscapePhoneMenu) {
        _portraitMenu = portraitMenu;
        _landscapePhoneMenu = landscapePhoneMenu;
        _navigation.axis = phone ? UILayoutConstraintAxisHorizontal
                                        : UILayoutConstraintAxisVertical;
        _navigation.distribution = phone ? UIStackViewDistributionFillEqually
                                                : UIStackViewDistributionFill;
        _navigation.spacing = portraitMenu ? 3 : 5;
        for (UIButton *tab in _tabs) {
            UIButtonConfiguration *config = tab.configuration;
            config.imagePlacement = portraitMenu ? NSDirectionalRectEdgeTop
                                                 : NSDirectionalRectEdgeLeading;
            config.imagePadding = portraitMenu ? 3 : landscapePhoneMenu ? 6 : 12;
            config.preferredSymbolConfigurationForImage = [UIImageSymbolConfiguration configurationWithPointSize:phone ? 16 : 22];
            config.contentInsets = portraitMenu
                ? NSDirectionalEdgeInsetsMake(5, 2, 5, 2)
                : landscapePhoneMenu ? NSDirectionalEdgeInsetsMake(8, 6, 8, 6)
                                     : NSDirectionalEdgeInsetsMake(13, 12, 13, 12);
            config.titleTextAttributesTransformer = ^NSDictionary *(NSDictionary *attributes) {
                NSMutableDictionary *updated = [attributes mutableCopy];
                updated[NSFontAttributeName] = [UIFont monospacedSystemFontOfSize:
                    portraitMenu ? 10 : landscapePhoneMenu ? 11 : 12 weight:UIFontWeightBold];
                return updated;
            };
            tab.configuration = config;
            tab.contentHorizontalAlignment = phone
                ? UIControlContentHorizontalAlignmentCenter
                : UIControlContentHorizontalAlignmentLeading;
        }
        _wordmark.font = [UIFont fontWithName:@"HelveticaNeue-CondensedBlack"
            size:(portraitMenu ? 52 : 67)] ?: [UIFont systemFontOfSize:
                (portraitMenu ? 52 : 67) weight:UIFontWeightBlack];
        _playHeadline.text = landscapePhoneMenu ? @"Liberty City awaits."
                                                : @"One more night\nin Liberty City.";
        _playHeadline.font = [UIFontMetrics.defaultMetrics scaledFontForFont:
            [UIFont systemFontOfSize:(landscapePhoneMenu ? 22 : portraitMenu ? 26 : 31)
                             weight:UIFontWeightSemibold]];
        _playIntro.hidden = landscapePhoneMenu;
        _playSaveNote.hidden = landscapePhoneMenu;
        _play.spacing = landscapePhoneMenu ? 12 : portraitMenu ? 16 : 22;
    }

    // Overscan the decorative world so an orbit never exposes the edge of the scene view.
    _city.frame = CGRectMake(compact ? -w * .20 : w * .25, -h * .08,
                             compact ? w * 1.40 : w * .92, h * 1.16);
    _atmosphere.frame = self.bounds;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    _shade.frame = self.bounds;
    _rain.frame = self.bounds;
    _rain.emitterPosition = CGPointMake(w * .66, -24);
    _rain.emitterSize = CGSizeMake(w * .75, 1);
    _shade.opacity = compact ? .98 : 1;
    [CATransaction commit];

    _masthead.text = phone ? @"THEFT4  /  LIBERTY CITY" : @"T H E F T 4   /   LIBERTY CITY";
    _masthead.frame = CGRectMake(inset, top, w - 2 * inset, 20);
    _edition.hidden = compact;
    _edition.frame = CGRectMake(w - 350, top, 350 - inset, 20);
    _topRule.frame = CGRectMake(inset, top + 34, w - 2 * inset, 1);

    CGFloat panelTop = top + (phone ? 28 : 51);
    CGFloat panelBottom = h - bottom - (phone ? 40 : 60);
    CGFloat panelWidth = landscapePhoneMenu ? w - 2 * inset
        : compact ? MIN(w - 2 * inset, 650) : MIN(w * .61, 740);
    _menuPanel.frame = CGRectMake(inset, panelTop, panelWidth,
        portraitMenu ? MAX(250, panelBottom - panelTop)
                     : landscapePhoneMenu ? MAX(230, panelBottom - panelTop)
                                          : MAX(300, panelBottom - panelTop));
    CGFloat pw = _menuPanel.bounds.size.width, ph = _menuPanel.bounds.size.height;
    _wordmark.hidden = phone;
    if (phone) {
        CGFloat barHeight = portraitMenu ? 56 : 48;
        _navigationScroll.frame = CGRectMake(8, 6, pw - 16, barHeight);
        _navigation.frame = CGRectMake(0, 0, pw - 16, barHeight);
        _navigationScroll.contentSize = _navigation.bounds.size;
        _navRule.frame = CGRectMake(12, barHeight + 14, pw - 24, 1);
        _controllerHint.hidden = YES;
        _pageTitle.frame = CGRectMake(14, barHeight + 22, pw - 28, 18);
        _pageDetail.hidden = YES;
        _scroll.frame = CGRectMake(12, barHeight + 48, pw - 24, MAX(40, ph - barHeight - 60));
    } else {
        _pageDetail.hidden = NO;
        CGFloat navWidth = compact ? 145 : 174;
        _wordmark.frame = CGRectMake(22, 12, pw - 44, 81);
        _navigationScroll.frame = CGRectMake(12, landscapePhoneMenu ? 10 : 120,
            navWidth - 16, MIN(260, ph - (landscapePhoneMenu ? 20 : 174)));
        _navigation.frame = CGRectMake(0, 0, navWidth - 16, 260);
        _navigationScroll.contentSize = _navigation.bounds.size;
        _navRule.frame = CGRectMake(navWidth, landscapePhoneMenu ? 10 : 109,
            1, ph - (landscapePhoneMenu ? 20 : 127));
        _controllerHint.hidden = landscapePhoneMenu;
        _controllerHint.frame = CGRectMake(17, ph - 42, navWidth - 26, 30);
        _pageTitle.frame = CGRectMake(navWidth + 24, landscapePhoneMenu ? 10 : 111,
            pw - navWidth - 44, 20);
        _pageDetail.frame = CGRectMake(navWidth + 24, landscapePhoneMenu ? 33 : 134,
            pw - navWidth - 44, 18);
        _scroll.frame = CGRectMake(navWidth + 24, landscapePhoneMenu ? 63 : 168,
            pw - navWidth - 39, ph - (landscapePhoneMenu ? 73 : 186));
    }

    _bottomRule.frame = CGRectMake(inset, h - bottom - (phone ? 34 : 46), w - 2 * inset, 1);
    _statusLabel.frame = CGRectMake(inset, h - bottom - (phone ? 29 : 39), compact ? w - 2 * inset : w * .55, phone ? 30 : 38);
    _configuration.hidden = compact;
    _configuration.frame = CGRectMake(w * .55, h - bottom - 39, w * .45 - inset, 38);
    _sceneCaption.hidden = compact;
    _sceneCaption.frame = CGRectMake(w - 390, h - bottom - 114, 390 - inset, 48);
    [self refreshConfigurationSummary];
}

- (uint32_t)renderHeight {
    NSInteger index = _renderResolution.selectedSegmentIndex;
    return index == 0 ? 540 : index == 2 ? 900 : index == 3 ? 1080 :
        index == 4 ? THEFT4_LAB_NATIVE_16_9 : 720;
}

- (void)refreshConfigurationSummary {
    if (_sharpening) {
        const NSInteger strength = (NSInteger)lroundf(_sharpening.value);
        _sharpeningMetrics.text = strength ? [NSString stringWithFormat:@"%ld%% · NEXT GAME LAUNCH", (long)strength] : @"OFF · 0%";
        _sharpening.accessibilityValue = [NSString stringWithFormat:@"%ld percent", (long)strength];
    }
    NSArray<NSString *> *shadowMetrics = @[
        @"128 base · 1024 × 1024 cache · 0.75× range",
        @"256 base · 2048 × 2048 cache · 1× range",
        @"512 base · 4096 × 4096 cache · 1× range",
        @"1024 base · up to 8192 × 8192 cache · 1.5× range"];
    NSArray<NSString *> *distanceMetrics = @[
        kOptimizedDistanceMetrics,
        @"1× world distance · 13,000 drawable references",
        @"2× world distance · 17,000 drawable references",
        @"3× world distance · 20,000 drawable references"];
    NSArray<NSString *> *reflectionMetrics = @[
        @"320 × 180 mirror/water · 256² environment",
        @"1920 × 1080 mirror/water · 1024² environment",
        @"2560 × 1440 mirror/water · 2048² environment"];
    _shadowMetrics.text = shadowMetrics[MAX(0, MIN(3, _shadowQuality.selectedSegmentIndex))];
    _distanceMetrics.text = distanceMetrics[MAX(0, MIN(3, _drawDistance.selectedSegmentIndex))];
    _modelMetrics.text = _modelDetail.selectedSegmentIndex == 0
        ? @"Earlier transition to simpler resident meshes · 1.75× selector input"
        : _modelDetail.selectedSegmentIndex == 2
            ? @"Highest resident mesh at any distance" : @"Title-controlled model LOD";
    _reflectionMetrics.text = reflectionMetrics[MAX(0, MIN(2, _reflectionQuality.selectedSegmentIndex))];
    _aaMetrics.text = @[@"No edge filter", @"FXAA · single lightweight pass",
                        @"SMAA 1× · high preset"][MAX(0, MIN(2, _antiAliasing.selectedSegmentIndex))];
    NSString *shadow = @[@"OPTIMIZED SHADOWS", @"ORIGINAL SHADOWS", @"ENHANCED SHADOWS", @"ULTRA SHADOWS"]
        [MAX(0, _shadowQuality.selectedSegmentIndex)];
    NSString *distance = @[@"OPTIMIZED DISTANCE", @"ORIGINAL DISTANCE", @"2× DISTANCE", @"3× DISTANCE"]
        [MAX(0, _drawDistance.selectedSegmentIndex)];
    if (_renderResolution) {
        const BOOL native = _renderResolution.selectedSegmentIndex == 4;
        _fsrUpscaling.enabled = !native;
        if (native) _fsrUpscaling.on = NO;
        UIScreen *screen = self.window.screen ?: UIScreen.mainScreen;
        const char *deviceProfile = getenv("THEFT4_DEVICE_PROFILE") ?: "";
        const BOOL fixed1080Output = strcmp(deviceProfile, "a19") == 0 ||
            strcmp(deviceProfile, "iphone-6gb") == 0 ||
            strcmp(deviceProfile, "legacy-ipad") == 0;
        const uint32_t fullWidth = (uint32_t)floor(self.bounds.size.width * screen.nativeScale);
        const uint32_t fullHeight = (uint32_t)floor(self.bounds.size.height * screen.nativeScale);
        uint32_t units = MIN(fullWidth / 16, fullHeight / 9);
        if (!units) units = 1;
        const uint32_t nativeWidth = _nativeAspect.on ? fullWidth : units * 16;
        const uint32_t nativeHeight = _nativeAspect.on ? fullHeight : units * 9;
        const theft4_output_policy output = _nativeAspect.on
            ? theft4_output_policy_for_native_aspect_lab(self.renderHeight,
                _fsrUpscaling.on, nativeWidth, nativeHeight, fixed1080Output)
            : fixed1080Output && !native
            ? theft4_output_policy_for_fixed_1080_lab_selected_aspect(
                self.renderHeight, _fsrUpscaling.on, nativeWidth, nativeHeight)
            : theft4_output_policy_for_lab(self.renderHeight, _fsrUpscaling.on,
                nativeWidth, nativeHeight);
        _resolutionSummary.text = [NSString stringWithFormat:@"%u × %u  →  %u × %u\n%@ · NEXT GAME LAUNCH",
            output.render_width, output.render_height, output.output_width, output.output_height,
            [NSString stringWithFormat:@"%@ · %@", output.fsr1 ? @"FSR ON" : @"FSR OFF",
                output.native_aspect ? @"NATIVE ASPECT" : @"16:9"]];
        _configuration.text = [NSString stringWithFormat:@"%@ / %@ / %@",
            native ? @"NATIVE PIXELS" : [NSString stringWithFormat:@"%up", self.renderHeight],
            shadow, distance];
        return;
    }
    _configuration.text = [NSString stringWithFormat:@"720p / %@ / %@", shadow, distance];
}

- (void)motionChanged:(NSNotification *)note { [self setActive:_active]; }

- (void)setActive:(BOOL)active {
    _active = active;
    [_city setActive:active];
    _rain.birthRate = active && !_retired && !UIAccessibilityIsReduceMotionEnabled() ? 1 : 0;
    _rain.hidden = !active || _retired || UIAccessibilityIsReduceMotionEnabled();
}

- (void)retireScene {
    if (_retired) return;
    _retired = YES;
    [_city retire];
    [_city removeFromSuperview];
    _city = nil;
    _rain.emitterCells = nil;
    [_rain removeFromSuperlayer];
    _rain = nil;
}

- (void)dealloc {
    [NSNotificationCenter.defaultCenter removeObserver:self];
    [self retireScene];
}
@end
