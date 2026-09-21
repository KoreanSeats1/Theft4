#import "Theft4LauncherView.h"
#import "Theft4CityView.h"
#include "theft4_output_policy.h"
#include <stdlib.h>
#include <string.h>
#import <QuartzCore/QuartzCore.h>

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
    [control setTitleTextAttributes:@{NSForegroundColorAttributeName: Ink(0xD9D5C7)}
                           forState:UIControlStateNormal];
    [control setTitleTextAttributes:@{NSForegroundColorAttributeName: Ink(0x10191B)}
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
    UILabel *_shadowMetrics, *_distanceMetrics, *_modelMetrics, *_reflectionMetrics, *_aaMetrics;
    UIView *_topRule, *_bottomRule, *_navRule;
    UIScrollView *_scroll;
    UIStackView *_content, *_navigation;
    UIStackView *_play, *_graphics, *_interfacePage, *_system;
    UILabel *_playHeadline, *_playIntro, *_playSaveNote;
    NSArray<UIButton *> *_tabs;
    NSArray<UIView *> *_pages;
    BOOL _active, _retired, _portraitMenu, _landscapePhoneMenu;
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
    BOOL lab = [NSBundle.mainBundle.infoDictionary[@"Theft4EnhancedBuild"] boolValue];
    BOOL isolatedLab = [NSBundle.mainBundle.infoDictionary[@"Theft4LabBuild"] boolValue];
    NSString *version = NSBundle.mainBundle.infoDictionary[@"CFBundleShortVersionString"] ?: @"0";
    NSString *build = NSBundle.mainBundle.infoDictionary[@"CFBundleVersion"] ?: @"0";
    _edition = Copy([NSString stringWithFormat:isolatedLab
        ? @"M5 LAB   /   v%@ (%@)   /   EXPERIMENTAL"
        : @"AFTER HOURS   /   v%@ (%@)", version, build], 11, YES);
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
    NSArray<NSString *> *names = @[@"PLAY", @"GRAPHICS", @"INTERFACE", @"SYSTEM"];
    NSArray<NSString *> *symbols = @[@"play.fill", @"slider.horizontal.3", @"rectangle.on.rectangle", @"wrench.and.screwdriver"];
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
    [_menuPanel addSubview:_navigation];
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

    _showFrameTime = [UISwitch new];
    _showFPS = [UISwitch new];
    _showControls = [UISwitch new];
    _anisotropicFiltering = [UISwitch new];
    _enhancedOutput = [UISwitch new];
    _fsrBoost = [UISwitch new];
    _motionBlur = [UISwitch new];
    NSArray<UISwitch *> *switches = @[_showFrameTime, _showFPS, _showControls,
        _anisotropicFiltering, _enhancedOutput, _fsrBoost, _motionBlur];
    NSArray<NSString *> *identifiers = @[@"showFrameTime", @"showFPS", @"showTouchControls",
        @"anisotropicFiltering", @"enhancedOutput1080p", @"fsrBoost", @"motionBlur"];
    for (NSUInteger i = 0; i < switches.count; ++i) {
        switches[i].onTintColor = Ink(0xB6884D);
        switches[i].accessibilityIdentifier = [@"settings." stringByAppendingString:identifiers[i]];
    }

    NSMutableArray<UIView *> *graphicsRows = [NSMutableArray new];
    if (lab) {
        _renderResolution = ChoiceControl(@[@"540p", @"720p", @"900p", @"1080p"], @"renderResolution",
            @"Render resolution", @"The internal scene resolution. Applies at the next game launch.");
        _renderResolution.selectedSegmentIndex = 1;
        _fsrUpscaling = [UISwitch new];
        _fsrUpscaling.onTintColor = Ink(0xB6884D);
        _fsrUpscaling.accessibilityIdentifier = @"settings.fsrUpscaling";
        _resolutionSummary = Copy(@"", 12, YES);
        _resolutionSummary.accessibilityIdentifier = @"settings.resolutionSummary";
        [graphicsRows addObjectsFromArray:@[
            [self choice:@"INTERNAL RESOLUTION" detail:@"540p = 960 × 540, 56% of 720p's pixels. 720p = 1280 × 720. 900p and 1080p cost more GPU time." control:_renderResolution],
            [self setting:@"FSR UPSCALING" detail:
                (strcmp(getenv("THEFT4_DEVICE_PROFILE") ?: "", "a19") == 0
                    ? @"A19: 540p, 720p and 900p upscale to 1080p; 1080p renders natively."
                    : @"Fit the selected internal resolution to the display with spatial upscaling.")
                toggle:_fsrUpscaling],
            _resolutionSummary
        ]];
        _lowPowerButton = Action(@"APPLY PERFORMANCE PRESET", NO);
        _lowPowerButton.accessibilityIdentifier = @"settings.lowPowerPreset";
        [graphicsRows addObjectsFromArray:@[
            _lowPowerButton,
            Copy(@"540p + FSR, original shadows/distance/reflections/model LOD, no edge filter, 1× texture filtering and no motion blur. You can adjust each setting afterward.", 12, NO)
        ]];
    }

    _shadowQuality = ChoiceControl(@[@"Original", @"Enhanced", @"Ultra"], @"shadowQuality",
        @"Dynamic shadows", @"Select original, enhanced, or ultra shadow-map resolution and range.");
    _drawDistance = ChoiceControl(@[@"Original", @"2×", @"3×"], @"drawDistance",
        @"Draw distance", @"Extend GTA IV's built-in world-distance input.");
    _modelDetail = ChoiceControl(@[@"Original", @"Highest LOD"], @"modelDetail",
        @"Model detail", @"Prefer the highest resident model detail level.");
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
        [self choice:@"DYNAMIC SHADOWS" detail:@"Original: 256 base / 2048² point cache. Enhanced: 512 / 4096². Ultra: 1024 / 8192²; device limits may cap it." control:_shadowQuality metrics:_shadowMetrics],
        [self choice:@"DRAW DISTANCE" detail:@"World-distance multiplier and drawable-reference capacity. Longer range adds CPU, streaming and draw work." control:_drawDistance metrics:_distanceMetrics],
        [self choice:@"MODEL DETAIL" detail:@"Highest LOD selects the best resident model; it does not force missing models to load." control:_modelDetail metrics:_modelMetrics],
        [self choice:@"REFLECTION QUALITY" detail:@"Mirror and water targets / environment cubemap. Full is capped at 1440p in this build." control:_reflectionQuality metrics:_reflectionMetrics],
        [self choice:@"ANTI-ALIASING" detail:@"Edge smoothing after scene rendering; this does not change internal resolution." control:_antiAliasing metrics:_aaMetrics],
        [self setting:@"TEXTURE FILTERING · 4×" detail:@"Cleaner roads and surfaces at an angle." toggle:_anisotropicFiltering],
        [self setting:@"MOTION BLUR" detail:@"Original movement blur. Disable for a sharper image in motion." toggle:_motionBlur],
        Copy(@"Graphics changes apply on the next game launch. Extended distance and Ultra shadows can reduce frame rate in dense areas.", 12, NO)
    ]];
    if (!lab) {
        [graphicsRows insertObjects:@[
            [self setting:@"1080p ENHANCED OUTPUT" detail:@"FSR 1 upscale plus sharpening." toggle:_enhancedOutput],
            [self setting:@"EXPERIMENTAL FSR BOOST" detail:@"Native-pixel 16:9 output while retaining the 720p scene." toggle:_fsrBoost]
        ] atIndexes:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, 2)]];
    }
    _graphics = Column(graphicsRows, 20);

    _interfacePage = Column(@[
        [self setting:@"FRAME COUNTER" detail:@"Unique game frames in the top-right corner." toggle:_showFPS],
        [self setting:@"FRAME-TIME GRAPH" detail:@"Frame delivery against the 33.3 ms target. Double-tap it to record a performance capture." toggle:_showFrameTime],
        [self setting:@"TOUCH CONTROLS" detail:@"Physical controllers continue to work when the overlay is hidden." toggle:_showControls],
        Copy(@"A connected controller can move focus through this launcher. Use the D-pad or left stick to navigate and A to select.", 12, NO)
    ], 20);

    _prepareButton = Action(@"VERIFY GAME FILES", NO);
    _restartButton = Action(@"RESTART CORE PROBE", NO);
    _restartButton.accessibilityIdentifier = @"core.restart";
    _performanceCapture = [UISwitch new];
    _performanceCapture.onTintColor = Ink(0xB6884D);
    _performanceCapture.accessibilityIdentifier = @"settings.performanceCapture";
    _downloadLogButton = Action(@"DOWNLOAD LATEST LOG CAPTURE", NO);
    _downloadLogButton.accessibilityIdentifier = @"diagnostics.downloadLatestCapture";
    _detailLabel = Copy(@"Waiting for runtime information…", 12, YES);
    _detailLabel.accessibilityIdentifier = @"core.details";
    NSString *displayName = NSBundle.mainBundle.infoDictionary[@"CFBundleDisplayName"] ?: @"Theft4";
    _system = Column(@[
        Copy(@"RUNTIME", 13, YES),
        Copy(@"Native ARM64 game code. Your game files. Your city.", 17, NO),
        [self setting:@"DETAILED PERFORMANCE CAPTURE"
               detail:@"Enable before launching. In the slow scene, double-tap the frame-time graph to capture 600 detailed CPU/GPU frames. Wait for completion, then quit, reopen, and export. Profiling adds overhead."
               toggle:_performanceCapture],
        _downloadLogButton,
        _prepareButton, _restartButton, _detailLabel,
        Copy([NSString stringWithFormat:@"On first launch, %@ creates Files → On My iPhone/iPad → %@ → game. Copy the contents of the prepared game folder into game, then verify.", displayName, displayName], 13, NO)
    ], 20);

    _pages = @[_play, _graphics, _interfacePage, _system];
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
    NSArray<NSString *> *titles = @[@"PLAY", @"GRAPHICS", @"INTERFACE", @"SYSTEM"];
    NSArray<NSString *> *details = @[@"CONTINUE INTO LIBERTY CITY", @"IMAGE AND WORLD QUALITY",
        @"HUD AND INPUT", @"GAME FILES AND RUNTIME"];
    for (NSUInteger i = 0; i < _pages.count; ++i) _pages[i].hidden = i != sender.tag;
    _pageTitle.text = titles[sender.tag];
    _pageDetail.text = details[sender.tag];
    [self updateTabAppearance];
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

    if (_portraitMenu != portraitMenu || _landscapePhoneMenu != landscapePhoneMenu) {
        _portraitMenu = portraitMenu;
        _landscapePhoneMenu = landscapePhoneMenu;
        _navigation.axis = portraitMenu ? UILayoutConstraintAxisHorizontal
                                        : UILayoutConstraintAxisVertical;
        _navigation.distribution = portraitMenu ? UIStackViewDistributionFillEqually
                                                : UIStackViewDistributionFill;
        _navigation.spacing = portraitMenu ? 3 : 5;
        for (UIButton *tab in _tabs) {
            UIButtonConfiguration *config = tab.configuration;
            config.imagePlacement = portraitMenu ? NSDirectionalRectEdgeTop
                                                 : NSDirectionalRectEdgeLeading;
            config.imagePadding = portraitMenu ? 3 : 12;
            config.contentInsets = portraitMenu
                ? NSDirectionalEdgeInsetsMake(5, 2, 5, 2)
                : NSDirectionalEdgeInsetsMake(13, 12, 13, 12);
            config.titleTextAttributesTransformer = ^NSDictionary *(NSDictionary *attributes) {
                NSMutableDictionary *updated = [attributes mutableCopy];
                updated[NSFontAttributeName] = [UIFont monospacedSystemFontOfSize:
                    portraitMenu ? 10 : 12 weight:UIFontWeightBold];
                return updated;
            };
            tab.configuration = config;
            tab.contentHorizontalAlignment = portraitMenu
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

    _masthead.frame = CGRectMake(inset, top, w - 2 * inset, 20);
    _edition.hidden = compact;
    _edition.frame = CGRectMake(w - 350, top, 350 - inset, 20);
    _topRule.frame = CGRectMake(inset, top + 34, w - 2 * inset, 1);

    CGFloat panelTop = top + (portraitMenu ? 38 : landscapePhoneMenu ? 30 : 51);
    CGFloat panelBottom = h - bottom - (portraitMenu ? 55 : landscapePhoneMenu ? 50 : 60);
    CGFloat panelWidth = landscapePhoneMenu ? w - 2 * inset
        : compact ? MIN(w - 2 * inset, 650) : MIN(w * .61, 740);
    _menuPanel.frame = CGRectMake(inset, panelTop, panelWidth,
        portraitMenu ? MAX(250, panelBottom - panelTop)
                     : landscapePhoneMenu ? MAX(230, panelBottom - panelTop)
                                          : MAX(300, panelBottom - panelTop));
    CGFloat pw = _menuPanel.bounds.size.width, ph = _menuPanel.bounds.size.height;
    _wordmark.hidden = landscapePhoneMenu;
    if (portraitMenu) {
        _wordmark.frame = CGRectMake(18, 5, pw - 36, 64);
        _navigation.frame = CGRectMake(11, 70, pw - 22, 62);
        _navRule.frame = CGRectMake(16, 140, pw - 32, 1);
        _controllerHint.hidden = YES;
        _pageTitle.frame = CGRectMake(18, 150, pw - 36, 20);
        _pageDetail.frame = CGRectMake(18, 173, pw - 36, 18);
        _scroll.frame = CGRectMake(18, 201, pw - 36, MAX(40, ph - 216));
    } else {
        CGFloat navWidth = landscapePhoneMenu ? 145 : compact ? 145 : 174;
        _wordmark.frame = CGRectMake(22, 12, pw - 44, 81);
        _navigation.frame = CGRectMake(12, landscapePhoneMenu ? 10 : 120,
            navWidth - 16, 212);
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

    _bottomRule.frame = CGRectMake(inset, h - bottom - 46, w - 2 * inset, 1);
    _statusLabel.frame = CGRectMake(inset, h - bottom - 39, compact ? w - 2 * inset : w * .55, 38);
    _configuration.hidden = compact;
    _configuration.frame = CGRectMake(w * .55, h - bottom - 39, w * .45 - inset, 38);
    _sceneCaption.hidden = compact;
    _sceneCaption.frame = CGRectMake(w - 390, h - bottom - 114, 390 - inset, 48);
    [self refreshConfigurationSummary];
}

- (uint32_t)renderHeight {
    NSInteger index = _renderResolution.selectedSegmentIndex;
    return index == 0 ? 540 : index == 2 ? 900 : index == 3 ? 1080 : 720;
}

- (void)refreshConfigurationSummary {
    NSArray<NSString *> *shadowMetrics = @[
        @"256 base · 2048 × 2048 cache · 1× range",
        @"512 base · 4096 × 4096 cache · 1× range",
        @"1024 base · up to 8192 × 8192 cache · 1.5× range"];
    NSArray<NSString *> *distanceMetrics = @[
        @"1× world distance · 13,000 drawable references",
        @"2× world distance · 17,000 drawable references",
        @"3× world distance · 20,000 drawable references"];
    NSArray<NSString *> *reflectionMetrics = @[
        @"320 × 180 mirror/water · 256² environment",
        @"1920 × 1080 mirror/water · 1024² environment",
        @"2560 × 1440 mirror/water · 2048² environment"];
    _shadowMetrics.text = shadowMetrics[MAX(0, MIN(2, _shadowQuality.selectedSegmentIndex))];
    _distanceMetrics.text = distanceMetrics[MAX(0, MIN(2, _drawDistance.selectedSegmentIndex))];
    _modelMetrics.text = _modelDetail.selectedSegmentIndex == 1
        ? @"Highest resident mesh at any distance" : @"Title-controlled model LOD";
    _reflectionMetrics.text = reflectionMetrics[MAX(0, MIN(2, _reflectionQuality.selectedSegmentIndex))];
    _aaMetrics.text = @[@"No edge filter", @"FXAA · single lightweight pass",
                        @"SMAA 1× · high preset"][MAX(0, MIN(2, _antiAliasing.selectedSegmentIndex))];
    NSString *shadow = @[@"ORIGINAL SHADOWS", @"ENHANCED SHADOWS", @"ULTRA SHADOWS"]
        [MAX(0, _shadowQuality.selectedSegmentIndex)];
    NSString *distance = @[@"ORIGINAL DISTANCE", @"2× DISTANCE", @"3× DISTANCE"]
        [MAX(0, _drawDistance.selectedSegmentIndex)];
    if (_renderResolution) {
        UIScreen *screen = self.window.screen ?: UIScreen.mainScreen;
        const BOOL a19 = strcmp(getenv("THEFT4_DEVICE_PROFILE") ?: "", "a19") == 0;
        const theft4_output_policy output = a19
            ? theft4_output_policy_for_a19_lab(self.renderHeight)
            : theft4_output_policy_for_lab(self.renderHeight, _fsrUpscaling.on,
                (uint32_t)floor(self.bounds.size.width * screen.nativeScale),
                (uint32_t)floor(self.bounds.size.height * screen.nativeScale));
        _resolutionSummary.text = [NSString stringWithFormat:@"%u × %u  →  %u × %u\n%@ · NEXT GAME LAUNCH",
            output.render_width, output.render_height, output.output_width, output.output_height,
            output.fsr1 ? @"FSR ON" : @"NATIVE"];
        _configuration.text = [NSString stringWithFormat:@"%up / %@ / %@",
            self.renderHeight, shadow, distance];
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
