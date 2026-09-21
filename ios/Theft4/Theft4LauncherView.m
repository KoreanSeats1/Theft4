#import "Theft4LauncherView.h"
#import "Theft4CityView.h"
#include "theft4_output_policy.h"
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
    UIView *_topRule, *_bottomRule, *_navRule;
    UIScrollView *_scroll;
    UIStackView *_content, *_navigation;
    UIStackView *_play, *_graphics, *_interfacePage, *_system;
    NSArray<UIButton *> *_tabs;
    NSArray<UIView *> *_pages;
    BOOL _active, _retired;
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
    NSString *version = NSBundle.mainBundle.infoDictionary[@"CFBundleShortVersionString"] ?: @"0";
    NSString *build = NSBundle.mainBundle.infoDictionary[@"CFBundleVersion"] ?: @"0";
    _edition = Copy([NSString stringWithFormat:lab
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

    UILabel *headline = Copy(@"One more night\nin Liberty City.", 31, NO);
    headline.font = [UIFontMetrics.defaultMetrics scaledFontForFont:
        [UIFont systemFontOfSize:31 weight:UIFontWeightSemibold]];
    headline.textColor = Ink(0xECE7D7);
    UILabel *intro = Copy(@"Cross the river. Take the long way home.\nYour next story starts on these streets.", 15, NO);
    _startButton = Action(@"ENTER LIBERTY CITY", YES);
    _startButton.accessibilityIdentifier = @"game.start";
    UILabel *saveNote = Copy(@"Continue or begin a new story inside the game. Your saves stay with this app.", 12, NO);
    _play = Column(@[headline, intro, _startButton, saveNote], 22);

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
        _renderResolution = ChoiceControl(@[@"720p", @"900p", @"1080p"], @"renderResolution",
            @"Render resolution", @"The internal scene resolution. Applies at the next game launch.");
        _renderResolution.selectedSegmentIndex = 0;
        _fsrUpscaling = [UISwitch new];
        _fsrUpscaling.onTintColor = Ink(0xB6884D);
        _fsrUpscaling.accessibilityIdentifier = @"settings.fsrUpscaling";
        _resolutionSummary = Copy(@"", 12, YES);
        _resolutionSummary.accessibilityIdentifier = @"settings.resolutionSummary";
        [graphicsRows addObjectsFromArray:@[
            [self choice:@"INTERNAL RESOLUTION" detail:@"Scene resolution before presentation. 900p is the current M-series balance." control:_renderResolution],
            [self setting:@"FSR UPSCALING" detail:@"Fit the selected internal resolution to the display with spatial upscaling." toggle:_fsrUpscaling],
            _resolutionSummary
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

    [graphicsRows addObjectsFromArray:@[
        [self choice:@"DYNAMIC SHADOWS" detail:@"Enhanced doubles map resolution at the original range. Ultra uses 1024 maps with a modest range extension." control:_shadowQuality],
        [self choice:@"DRAW DISTANCE" detail:@"Uses the title's original world-distance control. Larger scenes increase CPU work." control:_drawDistance],
        [self choice:@"MODEL DETAIL" detail:@"Highest LOD keeps the best resident model mesh where available." control:_modelDetail],
        [self choice:@"REFLECTION QUALITY" detail:@"Raises mirror, water, and environment reflection resolution through the native renderer." control:_reflectionQuality],
        [self choice:@"ANTI-ALIASING" detail:@"SMAA gives the cleanest edges here. FXAA is lighter; Off is the fastest control." control:_antiAliasing],
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
        [self setting:@"FRAME-TIME GRAPH" detail:@"Frame delivery against the 33.3 ms target. Double-tap it to record a Lab capture." toggle:_showFrameTime],
        [self setting:@"TOUCH CONTROLS" detail:@"Physical controllers continue to work when the overlay is hidden." toggle:_showControls],
        Copy(@"A connected controller can move focus through this launcher. Use the D-pad or left stick to navigate and A to select.", 12, NO)
    ], 20);

    _prepareButton = Action(@"VERIFY GAME FILES", NO);
    _restartButton = Action(@"RESTART CORE PROBE", NO);
    _restartButton.accessibilityIdentifier = @"core.restart";
    _detailLabel = Copy(@"Waiting for runtime information…", 12, YES);
    _detailLabel.accessibilityIdentifier = @"core.details";
    NSString *displayName = NSBundle.mainBundle.infoDictionary[@"CFBundleDisplayName"] ?: @"Theft4";
    _system = Column(@[
        Copy(@"RUNTIME", 13, YES),
        Copy(@"Native ARM64 game code. Your game files. Your city.", 17, NO),
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
    CGFloat inset = w < 650 ? 22 : 42;
    CGFloat top = MAX(24, self.safeAreaInsets.top + 12);
    CGFloat bottom = MAX(20, self.safeAreaInsets.bottom + 10);
    BOOL compact = w < 850;

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

    CGFloat panelTop = top + 51;
    CGFloat panelBottom = h - bottom - 60;
    CGFloat panelWidth = compact ? MIN(w - 2 * inset, 650) : MIN(w * .61, 740);
    _menuPanel.frame = CGRectMake(inset, panelTop, panelWidth, MAX(300, panelBottom - panelTop));
    CGFloat pw = _menuPanel.bounds.size.width, ph = _menuPanel.bounds.size.height;
    CGFloat navWidth = compact ? 145 : 174;
    _wordmark.frame = CGRectMake(20, 16, navWidth - 24, 76);
    _navigation.frame = CGRectMake(12, 103, navWidth - 16, 212);
    _navRule.frame = CGRectMake(navWidth, 18, 1, ph - 36);
    _controllerHint.frame = CGRectMake(17, ph - 42, navWidth - 26, 30);
    _pageTitle.frame = CGRectMake(navWidth + 24, 20, pw - navWidth - 44, 20);
    _pageDetail.frame = CGRectMake(navWidth + 24, 43, pw - navWidth - 44, 18);
    _scroll.frame = CGRectMake(navWidth + 24, 76, pw - navWidth - 39, ph - 94);

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
    return index == 1 ? 900 : index == 2 ? 1080 : 720;
}

- (void)refreshConfigurationSummary {
    NSString *shadow = @[@"ORIGINAL SHADOWS", @"ENHANCED SHADOWS", @"ULTRA SHADOWS"]
        [MAX(0, _shadowQuality.selectedSegmentIndex)];
    NSString *distance = @[@"ORIGINAL DISTANCE", @"2× DISTANCE", @"3× DISTANCE"]
        [MAX(0, _drawDistance.selectedSegmentIndex)];
    if (_renderResolution) {
        UIScreen *screen = self.window.screen ?: UIScreen.mainScreen;
        const theft4_output_policy output = theft4_output_policy_for_lab(
            self.renderHeight, _fsrUpscaling.on,
            (uint32_t)floor(self.bounds.size.width * screen.nativeScale),
            (uint32_t)floor(self.bounds.size.height * screen.nativeScale));
        _resolutionSummary.text = [NSString stringWithFormat:@"%u × %u  →  %u × %u\n%@ · NEXT GAME LAUNCH",
            output.render_width, output.render_height, output.output_width, output.output_height,
            _fsrUpscaling.on ? @"FSR ON" : @"FSR OFF"];
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
