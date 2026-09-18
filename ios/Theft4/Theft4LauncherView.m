#import "Theft4LauncherView.h"
#import "Theft4CityView.h"
#include "theft4_output_policy.h"
#import <QuartzCore/QuartzCore.h>

static UIColor *Ink(unsigned rgb) {
    return [UIColor colorWithRed:((rgb>>16)&255)/255.0 green:((rgb>>8)&255)/255.0
                           blue:(rgb&255)/255.0 alpha:1];
}
static UILabel *Copy(NSString *text, CGFloat size, BOOL mono) {
    UILabel *label = [UILabel new]; label.text = text; label.numberOfLines = 0;
    UIFont *font = mono ? [UIFont monospacedSystemFontOfSize:size weight:UIFontWeightMedium]
                       : [UIFont systemFontOfSize:size weight:UIFontWeightRegular];
    label.font = [[UIFontMetrics metricsForTextStyle:UIFontTextStyleBody] scaledFontForFont:font];
    label.adjustsFontForContentSizeCategory = YES;
    label.textColor = Ink(0xABB7B5);
    return label;
}
static UIButton *Action(NSString *title, BOOL primary) {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
    UIButtonConfiguration *config = [UIButtonConfiguration plainButtonConfiguration];
    config.title = title; config.cornerStyle = UIButtonConfigurationCornerStyleFixed;
    config.background.cornerRadius = 2;
    config.baseForegroundColor = primary ? Ink(0x111B1D) : Ink(0xE5DECA);
    config.background.backgroundColor = primary ? Ink(0xE6B56B) : Ink(0x1B292C);
    config.contentInsets = NSDirectionalEdgeInsetsMake(17,18,17,18);
    config.image = [UIImage systemImageNamed:primary ? @"arrow.up.right" : @"arrow.right"];
    config.imagePlacement = NSDirectionalRectEdgeTrailing; config.imagePadding = 18;
    button.configuration = config;
    button.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeading;
    [button.heightAnchor constraintGreaterThanOrEqualToConstant:54].active = YES;
    return button;
}
static UIStackView *Column(NSArray<UIView *> *views, CGFloat spacing) {
    UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:views];
    stack.axis = UILayoutConstraintAxisVertical; stack.spacing = spacing;
    return stack;
}

@implementation Theft4LauncherView {
    Theft4CityView *_city;
    UIView *_atmosphere;
    CAGradientLayer *_shade;
    CAEmitterLayer *_rain;
    UILabel *_masthead, *_edition, *_wordmark, *_sceneCaption, *_configuration;
    UILabel *_resolutionSummary;
    UIView *_topRule, *_bottomRule;
    UIScrollView *_scroll;
    UIStackView *_content, *_navigation, *_play, *_display, *_system;
    NSArray<UIButton *> *_tabs;
    BOOL _active, _retired;
}
- (instancetype)initWithFrame:(CGRect)frame {
    if (!(self = [super initWithFrame:frame])) return nil;
    self.backgroundColor = Ink(0x0B1418);
    _city = [Theft4CityView new]; [self addSubview:_city];
    _atmosphere = [UIView new]; _atmosphere.userInteractionEnabled = NO;
    [self addSubview:_atmosphere];
    _shade = [CAGradientLayer layer];
    _shade.colors = @[(id)Ink(0x0B1418).CGColor,
        (id)[Ink(0x0B1418) colorWithAlphaComponent:.97].CGColor,
        (id)[Ink(0x0B1418) colorWithAlphaComponent:.10].CGColor];
    _shade.locations = @[@0,@.30,@.82];
    _shade.startPoint = CGPointMake(0,.5); _shade.endPoint = CGPointMake(1,.5);
    [_atmosphere.layer addSublayer:_shade];
    UIGraphicsImageRenderer *renderer = [[UIGraphicsImageRenderer alloc] initWithSize:CGSizeMake(2,22)];
    UIImage *drop = [renderer imageWithActions:^(UIGraphicsImageRendererContext *ctx) {
        [[UIColor colorWithWhite:1 alpha:.5] setFill];
        [[UIBezierPath bezierPathWithRoundedRect:CGRectMake(0,0,1,22) cornerRadius:.5] fill];
    }];
    CAEmitterCell *rain = [CAEmitterCell emitterCell]; rain.contents = (id)drop.CGImage;
    rain.birthRate = 22; rain.lifetime = 5; rain.velocity = 190; rain.velocityRange = 60;
    rain.emissionLongitude = M_PI_2 + .12; rain.scale = .7; rain.scaleRange = .4;
    rain.color = [Ink(0x99BABC) colorWithAlphaComponent:.12].CGColor;
    _rain = [CAEmitterLayer layer]; _rain.emitterShape = kCAEmitterLayerLine;
    _rain.emitterCells = @[rain]; [_atmosphere.layer addSublayer:_rain];

    _masthead = Copy(@"T H E F T 4   /   LIBERTY CITY ARCHIVE",11,YES);
    _masthead.textColor = Ink(0xE5DECA); [self addSubview:_masthead];
    BOOL lab = [NSBundle.mainBundle.infoDictionary[@"Theft4LabBuild"] boolValue];
    _edition = Copy(lab ? @"M5 LAB   /   EXPERIMENTAL" : @"AFTER HOURS   /   LC—04",11,YES);
    _edition.textAlignment = NSTextAlignmentRight; [self addSubview:_edition];
    _topRule = [UIView new]; _bottomRule = [UIView new];
    _topRule.backgroundColor = _bottomRule.backgroundColor = [Ink(0xB7BBA8) colorWithAlphaComponent:.22];
    [self addSubview:_topRule]; [self addSubview:_bottomRule];

    _wordmark = Copy(@"THEFT4",92,NO);
    _wordmark.font = [UIFont fontWithName:@"HelveticaNeue-CondensedBlack" size:92]
        ?: [UIFont systemFontOfSize:92 weight:UIFontWeightBlack];
    _wordmark.textColor = Ink(0xECE7D7); _wordmark.accessibilityLabel = @"Theft four";
    UILabel *kicker = Copy(@"A CITY THAT NEVER LETS GO.",11,YES); kicker.textColor = Ink(0xE6B56B);
    NSMutableArray *tabs = [NSMutableArray new];
    NSArray *names = @[@"01  PLAY",@"02  DISPLAY",@"03  SYSTEM"];
    for (NSInteger i=0;i<3;++i) {
        UIButton *tab = [UIButton buttonWithType:UIButtonTypeSystem]; tab.tag = i;
        [tab setTitle:names[i] forState:UIControlStateNormal];
        tab.titleLabel.font = [UIFont monospacedSystemFontOfSize:11 weight:UIFontWeightBold];
        [tab.heightAnchor constraintGreaterThanOrEqualToConstant:48].active = YES;
        [tab addTarget:self action:@selector(selectTab:) forControlEvents:UIControlEventTouchUpInside];
        tab.accessibilityIdentifier = [@"launcher.tab." stringByAppendingFormat:@"%ld",(long)i];
        [tabs addObject:tab];
    }
    _tabs = tabs; _navigation = [[UIStackView alloc] initWithArrangedSubviews:tabs];
    _navigation.distribution = UIStackViewDistributionFillEqually; _navigation.spacing = 8;
    UILabel *headline = Copy(@"One more night\nin Liberty City.",31,NO);
    headline.font = [UIFontMetrics.defaultMetrics scaledFontForFont:
        [UIFont systemFontOfSize:31 weight:UIFontWeightSemibold]];
    headline.textColor = Ink(0xECE7D7);
    UILabel *intro = Copy(@"Cross the river. Take the long way home.\nYour next story starts on these streets.",15,NO);
    _startButton = Action(@"ENTER LIBERTY CITY",YES); _startButton.accessibilityIdentifier = @"game.start";
    UILabel *saveNote = Copy(@"Your saves stay with you. Continue or start a new story inside the game.",12,NO);
    _play = Column(@[headline,intro,_startButton,saveNote],22);

    _showFrameTime = [UISwitch new];
    _showFPS = [UISwitch new]; _showControls = [UISwitch new];
    _anisotropicFiltering = [UISwitch new]; _enhancedOutput = [UISwitch new]; _fsrBoost = [UISwitch new];
    _motionBlur = [UISwitch new];
    NSArray *switches = @[_showFrameTime,_showFPS,_showControls,_anisotropicFiltering,_enhancedOutput,_fsrBoost,_motionBlur];
    NSArray *identifiers = @[@"showFrameTime",@"showFPS",@"showTouchControls",@"anisotropicFiltering",@"enhancedOutput1080p",@"fsrBoost",@"motionBlur"];
    for (NSUInteger i=0;i<switches.count;++i) {
        UISwitch *toggle = switches[i]; toggle.onTintColor = Ink(0xB6884D);
        toggle.accessibilityIdentifier = [@"settings." stringByAppendingString:identifiers[i]];
    }
    NSMutableArray<UIView *> *displayRows = [NSMutableArray new];
    if (lab) {
        UILabel *resolutionTitle = Copy(@"Render resolution",16,NO);
        resolutionTitle.textColor = Ink(0xECE7D7);
        _renderResolution = [[UISegmentedControl alloc] initWithItems:@[@"720p",@"900p",@"1080p"]];
        _renderResolution.selectedSegmentIndex = 0;
        _renderResolution.selectedSegmentTintColor = Ink(0xB6884D);
        _renderResolution.backgroundColor = Ink(0x1B292C);
        [_renderResolution setTitleTextAttributes:@{NSForegroundColorAttributeName:Ink(0xECE7D7)}
            forState:UIControlStateNormal];
        [_renderResolution setTitleTextAttributes:@{NSForegroundColorAttributeName:Ink(0x111B1D)}
            forState:UIControlStateSelected];
        _renderResolution.accessibilityIdentifier = @"settings.renderResolution";
        _renderResolution.accessibilityLabel = @"Render resolution";
        _renderResolution.accessibilityHint = @"Choose 720p, 900p or 1080p. Applies at the next game launch.";
        [_renderResolution.heightAnchor constraintGreaterThanOrEqualToConstant:44].active = YES;
        _fsrUpscaling = [UISwitch new]; _fsrUpscaling.onTintColor = Ink(0xB6884D);
        _fsrUpscaling.accessibilityIdentifier = @"settings.fsrUpscaling";
        _resolutionSummary = Copy(@"",12,YES);
        _resolutionSummary.accessibilityIdentifier = @"settings.resolutionSummary";
        [displayRows addObjectsFromArray:@[
            Column(@[resolutionTitle,_renderResolution,
                Copy(@"Higher resolutions add scene detail and use more GPU power.",12,NO)],8),
            [self setting:@"FSR upscaling" detail:@"Upscale the selected resolution to fit the screen. Turn off for native rendering at the selected resolution." toggle:_fsrUpscaling],
            _resolutionSummary]];
    }
    [displayRows addObjectsFromArray:@[
        [self setting:@"Frame counter" detail:@"Unique game frames, shown in the top right." toggle:_showFPS],
        [self setting:@"Frame-time graph" detail:@"See brief spikes and the 33.3 ms target for 30 FPS." toggle:_showFrameTime],
        [self setting:@"Touch controls" detail:@"Physical controllers work with either setting." toggle:_showControls],
        [self setting:@"Texture filtering · 4×" detail:@"Cleaner roads and surfaces at an angle." toggle:_anisotropicFiltering],
        [self setting:@"Motion blur" detail:@"Original movement blur. Turn off for a sharper view in motion." toggle:_motionBlur]
    ]];
    if (!lab) {
        [displayRows addObjectsFromArray:@[
            [self setting:@"1080p enhanced output" detail:@"FSR 1 upscale + sharpening. The balanced default." toggle:_enhancedOutput],
            [self setting:@"Experimental FSR Boost" detail:@"Native-pixel 16:9 output. More output pixels; potentially less performance. Still rendered at 720p." toggle:_fsrBoost]]];
    }
    [displayRows addObject:Copy(@"Resolution, FSR, filtering and motion blur apply at the next game launch. FSR is spatial upscaling, not frame generation.",12,NO)];
    _display = Column(displayRows,18);
    _prepareButton = Action(@"VERIFY GAME FILES",NO);
    _restartButton = Action(@"RESTART CORE PROBE",NO); _restartButton.accessibilityIdentifier = @"core.restart";
    _detailLabel = Copy(@"Waiting for runtime information…",12,YES);
    _detailLabel.accessibilityIdentifier = @"core.details";
    _system = Column(@[
        Copy(@"UNDER THE HOOD",13,YES),
        Copy(@"Native ARM64 game code. Your own game files. Your own city.",17,NO),
        _prepareButton,_restartButton,_detailLabel,
        Copy([NSString stringWithFormat:@"On first launch, %@ creates Files → On My iPhone/iPad → %@ → game. Copy the contents of your prepared game folder into game, then verify. Saves remain private.",
              NSBundle.mainBundle.infoDictionary[@"CFBundleDisplayName"] ?: @"Theft4",
              NSBundle.mainBundle.infoDictionary[@"CFBundleDisplayName"] ?: @"Theft4"],13,NO)
    ],20);
    _scroll = [UIScrollView new]; _scroll.showsVerticalScrollIndicator = NO;
    _scroll.alwaysBounceVertical = NO; [self addSubview:_scroll];
    _content = Column(@[_wordmark,kicker,_navigation,_play,_display,_system],16);
    _content.translatesAutoresizingMaskIntoConstraints = NO; [_scroll addSubview:_content];
    [NSLayoutConstraint activateConstraints:@[
        [_content.topAnchor constraintEqualToAnchor:_scroll.contentLayoutGuide.topAnchor],
        [_content.bottomAnchor constraintEqualToAnchor:_scroll.contentLayoutGuide.bottomAnchor constant:-24],
        [_content.leadingAnchor constraintEqualToAnchor:_scroll.contentLayoutGuide.leadingAnchor],
        [_content.trailingAnchor constraintEqualToAnchor:_scroll.contentLayoutGuide.trailingAnchor],
        [_content.widthAnchor constraintEqualToAnchor:_scroll.frameLayoutGuide.widthAnchor]
    ]];
    _statusLabel = Copy(@"Core ready",12,YES); _statusLabel.accessibilityIdentifier = @"core.status";
    _statusLabel.numberOfLines = 2; [self addSubview:_statusLabel];
    _configuration = Copy(@"",10,YES); _configuration.textAlignment = NSTextAlignmentRight;
    [self addSubview:_configuration];
    _sceneCaption = Copy(@"THE EAST RIVER, AFTER DARK\nDRAG TO EXPLORE THE CITY",10,YES);
    _sceneCaption.textAlignment = NSTextAlignmentRight; [self addSubview:_sceneCaption];
    [self selectTab:_tabs[0]];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(motionChanged:)
        name:UIAccessibilityReduceMotionStatusDidChangeNotification object:nil];
    [self setActive:YES];
    return self;
}
- (UIView *)setting:(NSString *)title detail:(NSString *)detail toggle:(UISwitch *)toggle {
    UILabel *label = Copy(title,16,NO); label.textColor = Ink(0xECE7D7);
    UIStackView *text = Column(@[label,Copy(detail,12,NO)],5);
    UIStackView *row = [[UIStackView alloc] initWithArrangedSubviews:@[text,toggle]];
    row.alignment = UIStackViewAlignmentCenter; row.spacing = 16;
    toggle.accessibilityLabel = title; toggle.accessibilityHint = detail;
    [toggle setContentHuggingPriority:UILayoutPriorityRequired forAxis:UILayoutConstraintAxisHorizontal];
    [toggle setContentCompressionResistancePriority:UILayoutPriorityRequired forAxis:UILayoutConstraintAxisHorizontal];
    return row;
}
- (void)selectTab:(UIButton *)sender {
    _play.hidden = sender.tag != 0; _display.hidden = sender.tag != 1; _system.hidden = sender.tag != 2;
    for (UIButton *tab in _tabs) {
        BOOL selected = tab == sender;
        [tab setTitleColor:Ink(selected ? 0xE6B56B : 0x889998) forState:UIControlStateNormal];
        tab.backgroundColor = selected ? [Ink(0xE6B56B) colorWithAlphaComponent:.09] : UIColor.clearColor;
        tab.accessibilityTraits = UIAccessibilityTraitButton | (selected ? UIAccessibilityTraitSelected : 0);
    }
    [_scroll setContentOffset:CGPointZero animated:NO];
}
- (void)layoutSubviews {
    [super layoutSubviews];
    CGFloat w=self.bounds.size.width,h=self.bounds.size.height;
    CGFloat inset= w<600 ? 24 : 48;
    CGFloat top=MAX(28,self.safeAreaInsets.top+14), bottom=MAX(22,self.safeAreaInsets.bottom+12);
    BOOL compact = w<800;
    _city.frame = CGRectMake(compact ? 0 : w*.22,0,compact ? w : w*.84,h);
    _atmosphere.frame = self.bounds;
    [CATransaction begin]; [CATransaction setDisableActions:YES];
    _shade.frame = self.bounds;
    _rain.frame = self.bounds; _rain.emitterPosition = CGPointMake(w*.65,-24);
    _rain.emitterSize = CGSizeMake(w*.7,1);
    _shade.opacity = compact ? .97 : 1;
    [CATransaction commit];
    _masthead.frame = CGRectMake(inset,top,w-2*inset,20);
    _edition.hidden = compact; _edition.frame = CGRectMake(w-330,top,330-inset,20);
    _topRule.frame = CGRectMake(inset,top+36,w-2*inset,1);
    CGFloat contentWidth = compact ? MIN(440,w-2*inset) : MIN(440,w*.41);
    _scroll.frame = CGRectMake(inset,top+60,contentWidth,MAX(100,h-top-bottom-138));
    _bottomRule.frame = CGRectMake(inset,h-bottom-52,w-inset*2,1);
    _statusLabel.frame = CGRectMake(inset,h-bottom-44,compact ? w-inset*2 : w*.55,42);
    _configuration.hidden = compact;
    _configuration.frame = CGRectMake(w*.60,h-bottom-44,w*.40-inset,42);
    _sceneCaption.hidden = compact;
    _sceneCaption.frame = CGRectMake(w-350,h-bottom-125,350-inset,50);
    [self refreshConfigurationSummary];
}
- (uint32_t)renderHeight {
    NSInteger index = _renderResolution.selectedSegmentIndex;
    return index == 1 ? 900 : index == 2 ? 1080 : 720;
}
- (void)refreshConfigurationSummary {
    if (_renderResolution) {
        UIScreen *screen = self.window.screen ?: UIScreen.mainScreen;
        const theft4_output_policy output = theft4_output_policy_for_lab(
            self.renderHeight, _fsrUpscaling.on,
            (uint32_t)floor(self.bounds.size.width * screen.nativeScale),
            (uint32_t)floor(self.bounds.size.height * screen.nativeScale));
        _resolutionSummary.text = [NSString stringWithFormat:@"%u × %u → %u × %u\n%@ · NEXT GAME LAUNCH",
            output.render_width, output.render_height, output.output_width, output.output_height,
            _fsrUpscaling.on ? @"FSR ON" : @"FSR OFF"];
        _configuration.text = [NSString stringWithFormat:@"%up CORE  /  %@  /  %@",
            self.renderHeight, _fsrUpscaling.on ? @"FSR" : @"NATIVE",
            _anisotropicFiltering.on ? @"4× FILTER" : @"1× FILTER"];
        return;
    }
    _configuration.text = [NSString stringWithFormat:@"720p CORE  /  %@  /  %@",
        _fsrBoost.on ? @"FSR BOOST" : (_enhancedOutput.on ? @"1080p FSR" : @"720p OUTPUT"),
        _anisotropicFiltering.on ? @"4× FILTER" : @"1× FILTER"];
}
- (void)motionChanged:(NSNotification *)note { [self setActive:_active]; }
- (void)setActive:(BOOL)active {
    _active = active; [_city setActive:active];
    // Removing particles avoids stale animations across app backgrounding.
    _rain.birthRate = active && !_retired && !UIAccessibilityIsReduceMotionEnabled() ? 1 : 0;
    _rain.hidden = !active || _retired || UIAccessibilityIsReduceMotionEnabled();
}
- (void)retireScene {
    if (_retired) return;
    _retired = YES; [_city retire]; [_city removeFromSuperview]; _city = nil;
    _rain.emitterCells = nil; [_rain removeFromSuperlayer]; _rain = nil;
}
- (void)dealloc { [NSNotificationCenter.defaultCenter removeObserver:self]; [self retireScene]; }
@end
