#import "Theft4CityView.h"
#import <SceneKit/SceneKit.h>

static UIColor *CityColor(unsigned rgb) {
    return [UIColor colorWithRed:((rgb >> 16) & 255) / 255.0
                          green:((rgb >> 8) & 255) / 255.0 blue:(rgb & 255) / 255.0 alpha:1];
}

static SCNMaterial *CityMaterial(unsigned rgb, CGFloat roughness) {
    SCNMaterial *m = [SCNMaterial new];
    m.diffuse.contents = CityColor(rgb);
    m.roughness.contents = @(roughness);
    m.metalness.contents = @0.2;
    m.lightingModelName = SCNLightingModelPhysicallyBased;
    return m;
}

static SCNNode *CityBox(CGFloat w, CGFloat h, CGFloat d, SCNVector3 p, SCNMaterial *m) {
    SCNBox *box = [SCNBox boxWithWidth:w height:h length:d chamferRadius:0];
    box.materials = @[m];
    SCNNode *node = [SCNNode nodeWithGeometry:box];
    node.position = p;
    return node;
}

static void CityCable(SCNNode *root, SCNVector3 a, SCNVector3 b, SCNMaterial *m, CGFloat radius) {
    float dx=b.x-a.x, dy=b.y-a.y, dz=b.z-a.z;
    CGFloat distance = sqrt(dx*dx+dy*dy+dz*dz);
    SCNCylinder *cylinder = [SCNCylinder cylinderWithRadius:radius height:distance];
    cylinder.radialSegmentCount = 5;
    cylinder.materials = @[m];
    SCNNode *node = [SCNNode nodeWithGeometry:cylinder];
    node.position = SCNVector3Make((a.x+b.x)/2, (a.y+b.y)/2, (a.z+b.z)/2);
    [node lookAt:b up:SCNVector3Make(0,0,1) localFront:SCNVector3Make(0,1,0)];
    [root addChildNode:node];
}

// Original procedural window lights, not game art or a downloaded texture.
static UIImage *CityWindows(unsigned seed) {
    UIGraphicsImageRendererFormat *format = [UIGraphicsImageRendererFormat defaultFormat];
    format.scale = 1;
    UIGraphicsImageRenderer *renderer = [[UIGraphicsImageRenderer alloc]
        initWithSize:CGSizeMake(64, 128) format:format];
    return [renderer imageWithActions:^(UIGraphicsImageRendererContext *ctx) {
        [UIColor.blackColor setFill]; UIRectFill(CGRectMake(0,0,64,128));
        unsigned random = seed;
        for (int y=5; y<128; y+=10) for (int x=5; x<64; x+=10) {
            random = random * 1664525u + 1013904223u;
            if ((random >> 24) < 130) continue;
            [[CityColor((random & 1) ? 0xD1A66B : 0x91B1AE)
                colorWithAlphaComponent:0.25 + (random % 65)/100.0] setFill];
            UIRectFill(CGRectMake(x,y,3,5));
        }
    }];
}

@implementation Theft4CityView {
    SCNView *_sceneView;
    SCNNode *_orbit;
    SCNNode *_camera;
    UIVisualEffectView *_upperLensBlur;
    UIVisualEffectView *_lowerLensBlur;
    BOOL _retired;
    BOOL _active;
    CGFloat _panYawOrigin, _panPitchOrigin;
    CGFloat _yaw, _pitch;
    CGFloat _pinchOrigin, _zoom;
}

- (instancetype)initWithFrame:(CGRect)frame {
    if (!(self = [super initWithFrame:frame])) return nil;
    self.backgroundColor = CityColor(0x090F13);
    self.clipsToBounds = YES;
    self.accessibilityElementsHidden = YES;
    _sceneView = [[SCNView alloc] initWithFrame:self.bounds
        options:@{SCNPreferredRenderingAPIKey:@(SCNRenderingAPIMetal)}];
    _sceneView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _sceneView.backgroundColor = CityColor(0x090F13);
    _sceneView.preferredFramesPerSecond = 30;
    _sceneView.antialiasingMode = SCNAntialiasingModeMultisampling2X;
    _sceneView.userInteractionEnabled = NO;
    [self addSubview:_sceneView];
    SCNScene *scene = [SCNScene new];
    scene.background.contents = CityColor(0x090F13);
    scene.fogColor = CityColor(0x0C171C);
    scene.fogStartDistance = 35;
    scene.fogEndDistance = 112;
    scene.fogDensityExponent = 1.35;
    SCNNode *root = scene.rootNode;

    SCNNode *ambient = [SCNNode new];
    ambient.light = [SCNLight new]; ambient.light.type = SCNLightTypeAmbient;
    ambient.light.color = CityColor(0x769394); ambient.light.intensity = 410;
    [root addChildNode:ambient];
    SCNNode *key = [SCNNode new]; key.light = [SCNLight new];
    key.light.type = SCNLightTypeSpot; key.light.color = CityColor(0xFFD69A);
    key.light.intensity = 2200; key.light.spotInnerAngle = 45; key.light.spotOuterAngle = 95;
    key.light.castsShadow = YES; key.light.shadowMapSize = CGSizeMake(1024,1024);
    key.light.shadowRadius = 3; key.light.shadowColor = [UIColor colorWithWhite:0 alpha:0.65];
    key.position = SCNVector3Make(-8,24,12); [key lookAt:SCNVector3Make(0,0,-2)];
    [root addChildNode:key];
    SCNNode *rim = [SCNNode new]; rim.light = [SCNLight new];
    rim.light.type = SCNLightTypeOmni; rim.light.color = CityColor(0x739FAC);
    rim.light.intensity = 950; rim.position = SCNVector3Make(13,11,-10);
    [root addChildNode:rim];

    SCNFloor *water = [SCNFloor new];
    water.reflectivity = 0.23; water.reflectionResolutionScaleFactor = 0.3;
    water.reflectionFalloffEnd = 13;
    water.materials = @[CityMaterial(0x10252A, 0.22)];
    [root addChildNode:[SCNNode nodeWithGeometry:water]];
    SCNMaterial *land = CityMaterial(0x1A2425, 0.85);
    SCNMaterial *road = CityMaterial(0x0D1315, 0.55);
    SCNMaterial *iron = CityMaterial(0x56615B, 0.5);
    SCNMaterial *gold = CityMaterial(0xC99857, 0.4);
    gold.emission.contents = CityColor(0x805225);
    SCNMaterial *lamp = CityMaterial(0xE7C394, 0.4);
    lamp.emission.contents = CityColor(0xFFD6A0); lamp.emission.intensity = 2;

    SCNNode *staticCity = [SCNNode new];
    // The city now surrounds the camera pivot. The larger foundations and
    // distant districts keep the model continuous through a full 360° orbit.
    [staticCity addChildNode:CityBox(190,0.55,62,SCNVector3Make(0,0.28,-37),land)];
    [staticCity addChildNode:CityBox(190,0.55,62,SCNVector3Make(0,0.28,43),land)];
    NSMutableArray<SCNMaterial *> *facades = [NSMutableArray new];
    for (unsigned i=0;i<7;++i) {
        SCNMaterial *m = CityMaterial(i%2 ? 0x3E4946 : 0x323F43, 0.75);
        m.emission.contents = CityWindows(93+i*47); m.emission.intensity = 1.3;
        [facades addObject:m];
    }
    unsigned random = 414;
    for (int row=0;row<4;++row) for (int col=0;col<10;++col) {
        random = random*1664525u+1013904223u;
        CGFloat h=1.3+(random%95)/15.0, x=-14+col*3.0, z=-6-row*3.1;
        CGFloat w=1.35+(random%10)/12.0;
        if (col==5 && row<2) continue; // boulevard leading onto the bridge
        [staticCity addChildNode:CityBox(w,h,1.8,SCNVector3Make(x,h/2+0.55,z),facades[(row+col)%7])];
        [staticCity addChildNode:CityBox(w+0.08,0.13,1.88,SCNVector3Make(x,h+0.55,z),iron)];
        if (h>5) {
            [staticCity addChildNode:CityBox(w*.55,.8,1.1,SCNVector3Make(x,h+.95,z),land)];
            CityCable(staticCity,SCNVector3Make(x,h+1.3,z),SCNVector3Make(x,h+2.1,z),iron,.035);
        }
    }
    for (int col=0;col<7;++col) for (int row=0;row<2;++row) {
        CGFloat x=-13+col*4, z=7+row*4, h=1.1+(col%3)*.8;
        if (col==4) continue;
        [staticCity addChildNode:CityBox(2.5,h,2,SCNVector3Make(x,h/2+.55,z),facades[(col+row)%7])];
    }
    // Far districts are deliberately lower-detail silhouettes. They add depth
    // without making the launcher compete with the game for memory or GPU time.
    for (int district = 0; district < 4; ++district) {
        const BOOL alongX = district < 2;
        const CGFloat side = (district & 1) ? 1 : -1;
        for (int lane = -5; lane <= 5; ++lane) {
            for (int depth = 0; depth < 5; ++depth) {
                random = random * 1664525u + 1013904223u;
                CGFloat tangent = lane * 5.2 + ((depth & 1) ? 1.5 : 0);
                CGFloat radial = side * (18 + depth * 5.1);
                CGFloat x = alongX ? radial : tangent;
                CGFloat z = alongX ? tangent : radial;
                CGFloat h = 2.0 + (random % 125) / 14.0;
                CGFloat width = 2.1 + (random % 12) / 10.0;
                [staticCity addChildNode:CityBox(width, h, 2.5,
                    SCNVector3Make(x, h / 2 + .55, z), facades[(lane + depth + 14) % 7])];
                if ((random & 3) == 0) {
                    [staticCity addChildNode:CityBox(width * .55, .7, 1.2,
                        SCNVector3Make(x, h + .9, z), iron)];
                }
            }
        }
    }
    // A low-detail skyline continues around the whole horizon. Its outer
    // rings dissolve into atmospheric fog before a building-grid edge appears.
    for (int ring = 0; ring < 3; ++ring) {
        for (int segment = 0; segment < 28; ++segment) {
            random = random * 1664525u + 1013904223u;
            CGFloat angle = (CGFloat)segment * (2.0 * M_PI / 28.0) + ring * .055;
            CGFloat radius = 68 + ring * 15 + (random % 70) / 10.0;
            CGFloat x = cos(angle) * radius;
            CGFloat z = sin(angle) * radius;
            CGFloat h = 3.0 + (random % 105) / 13.0;
            CGFloat width = 3.2 + (random % 20) / 10.0;
            [staticCity addChildNode:CityBox(width, h, 3.1,
                SCNVector3Make(x, h / 2 + .55, z), facades[(segment + ring) % 7])];
        }
    }
    for (int row=0;row<4;++row)
        [staticCity addChildNode:CityBox(31,.02,.7,SCNVector3Make(-1,.57,-4.4-row*3.1),road)];
    for (int col=0;col<9;++col)
        [staticCity addChildNode:CityBox(.6,.02,13,SCNVector3Make(-12.5+col*3,.57,-11),road)];

    // Bespoke suspension bridge, the focal point rather than a stock city asset.
    [staticCity addChildNode:CityBox(2.5,.22,19,SCNVector3Make(1,1.35,0),road)];
    for (int s=-1;s<=1;s+=2) {
        CGFloat x=1+s*1.18;
        [staticCity addChildNode:CityBox(.08,.25,19,SCNVector3Make(x,1.55,0),iron)];
        for (int tower=-1;tower<=1;tower+=2) {
            CGFloat z=tower*4.3;
            [staticCity addChildNode:CityBox(.32,7.5,.65,SCNVector3Make(x,3.75,z),iron)];
            [staticCity addChildNode:CityBox(2.7,.28,.7,SCNVector3Make(1,7.0,z),iron)];
            [staticCity addChildNode:CityBox(2.7,.18,.7,SCNVector3Make(1,5.8,z),iron)];
            [staticCity addChildNode:CityBox(.22,.13,.25,SCNVector3Make(x,7.65,z),lamp)];
        }
        for (int k=0;k<28;++k) {
            CGFloat z=-9.4+k*(18.8/28), nz=z+18.8/28;
            CGFloat y=fabs(z)<=4.3 ? 2.9+4.6*pow(z/4.3,2) : 7.5-(fabs(z)-4.3)*1.13;
            CGFloat ny=fabs(nz)<=4.3 ? 2.9+4.6*pow(nz/4.3,2) : 7.5-(fabs(nz)-4.3)*1.13;
            CityCable(staticCity,SCNVector3Make(x,y,z),SCNVector3Make(x,ny,nz),gold,.027);
            CityCable(staticCity,SCNVector3Make(x,1.6,z),SCNVector3Make(x,y,z),iron,.015);
        }
    }
    for (int i=0;i<25;++i)
        [staticCity addChildNode:CityBox(.045,.015,.32,SCNVector3Make(1,1.472,-9+i*.75),gold)];
    for (int i=0;i<24;++i) {
        CGFloat x=-14+i*1.2;
        [staticCity addChildNode:CityBox(.07,.08,.07,SCNVector3Make(x,.75,-4.05),lamp)];
    }
    [root addChildNode:staticCity.flattenedClone];
    for (int i=0;i<10;++i) {
        CGFloat x=(i%2) ? .45 : 1.55, start=(i%2) ? -9 : 9;
        SCNNode *car = CityBox(.28,.16,.55,SCNVector3Make(x,1.55,start),iron);
        [car addChildNode:CityBox(.23,.055,.025,SCNVector3Make(0,0,(i%2)?.28:-.28),lamp)];
        SCNAction *drive = [SCNAction sequence:@[
            [SCNAction moveTo:SCNVector3Make(x,1.55,start) duration:0],
            [SCNAction moveTo:SCNVector3Make(x,1.55,-start) duration:10+i*.3]]];
        [car runAction:[SCNAction sequence:@[[SCNAction waitForDuration:i*.92],
            [SCNAction repeatActionForever:drive]]]];
        [root addChildNode:car];
    }
    _orbit = [SCNNode new]; [root addChildNode:_orbit];
    _camera = [SCNNode new]; _camera.camera = [SCNCamera new];
    _zoom = 1.0;
    _camera.position = SCNVector3Make(26,18,29);
    [_camera lookAt:SCNVector3Make(0,1,-2)];
    _camera.camera.fieldOfView = 43;
    _camera.camera.zNear = .1; _camera.camera.zFar = 220;
    _camera.camera.wantsHDR = YES; _camera.camera.wantsExposureAdaptation = NO;
    _camera.camera.exposureOffset = -.25;
    _camera.camera.bloomIntensity = .45; _camera.camera.bloomThreshold = 1;
    _camera.camera.bloomBlurRadius = 8;
    // Shallow focus keeps the bridge/city core legible and softens near and
    // distant blocks; the masked UIKit strips below shape it into a lens band.
    _camera.camera.wantsDepthOfField = YES; _camera.camera.focusDistance = 38;
    _camera.camera.fStop = 2.8; _camera.camera.focalBlurSampleCount = 12;
    _camera.camera.vignettingIntensity = .65; _camera.camera.vignettingPower = .8;
    [_orbit addChildNode:_camera]; _sceneView.pointOfView = _camera;
    _sceneView.scene = scene;
    UIBlurEffect *lens = [UIBlurEffect effectWithStyle:UIBlurEffectStyleSystemThinMaterialDark];
    _upperLensBlur = [[UIVisualEffectView alloc] initWithEffect:lens];
    _lowerLensBlur = [[UIVisualEffectView alloc] initWithEffect:lens];
    _upperLensBlur.userInteractionEnabled = NO;
    _lowerLensBlur.userInteractionEnabled = NO;
    [self addSubview:_upperLensBlur];
    [self addSubview:_lowerLensBlur];
    [self addGestureRecognizer:[[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(orbit:)]];
    [self addGestureRecognizer:[[UIPinchGestureRecognizer alloc] initWithTarget:self action:@selector(zoom:)]];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(motionChanged:)
        name:UIAccessibilityReduceMotionStatusDidChangeNotification object:nil];
    [self setActive:YES];
    return self;
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGFloat width = self.bounds.size.width, height = self.bounds.size.height;
    CGFloat band = height * .29;
    _upperLensBlur.frame = CGRectMake(0, 0, width, band);
    _lowerLensBlur.frame = CGRectMake(0, height - band, width, band);
    CAGradientLayer *upperMask = [CAGradientLayer layer];
    upperMask.frame = _upperLensBlur.bounds;
    upperMask.colors = @[(id)[UIColor colorWithWhite:1 alpha:.52].CGColor,
                         (id)UIColor.clearColor.CGColor];
    _upperLensBlur.layer.mask = upperMask;
    CAGradientLayer *lowerMask = [CAGradientLayer layer];
    lowerMask.frame = _lowerLensBlur.bounds;
    lowerMask.colors = @[(id)UIColor.clearColor.CGColor,
                         (id)[UIColor colorWithWhite:1 alpha:.54].CGColor];
    _lowerLensBlur.layer.mask = lowerMask;
}

- (void)orbit:(UIPanGestureRecognizer *)gesture {
    if (_retired) return;
    if (gesture.state==UIGestureRecognizerStateBegan) {
        [_orbit removeAllActions];
        _panYawOrigin = _yaw;
        _panPitchOrigin = _pitch;
    }
    CGPoint translation = [gesture translationInView:self];
    _yaw = fmod(_panYawOrigin + translation.x / 430.0, (CGFloat)(M_PI * 2.0));
    _pitch = fmax(-.22, fmin(.24, _panPitchOrigin + translation.y / 600.0));
    _orbit.eulerAngles = SCNVector3Make(_pitch, _yaw, 0);
}
- (void)zoom:(UIPinchGestureRecognizer *)gesture {
    if (_retired) return;
    if (gesture.state == UIGestureRecognizerStateBegan) _pinchOrigin = _zoom;
    _zoom = fmax(.72, fmin(1.38, _pinchOrigin / gesture.scale));
    _camera.position = SCNVector3Make(26 * _zoom, 18 * _zoom, 29 * _zoom);
    [_camera lookAt:SCNVector3Make(0,1,-2)];
}
- (void)motionChanged:(NSNotification *)note { [self setActive:_active]; }
- (void)setActive:(BOOL)active {
    _active = active;
    if (_retired) return;
    BOOL animated = active && !UIAccessibilityIsReduceMotionEnabled();
    _sceneView.scene.paused = !animated;
    _sceneView.playing = animated;
    _sceneView.rendersContinuously = animated;
    [_orbit removeAllActions];
    if (animated) {
        SCNAction *drift = [SCNAction rotateByX:0 y:.055 z:0 duration:12];
        drift.timingMode = SCNActionTimingModeEaseInEaseOut;
        [_orbit runAction:[SCNAction repeatActionForever:
            [SCNAction sequence:@[drift,drift.reversedAction]]]];
    }
}
- (void)retire {
    if (_retired) return;
    _retired = YES;
    _sceneView.playing = NO; _sceneView.scene.paused = YES;
    _sceneView.rendersContinuously = NO;
    [_sceneView.scene.rootNode removeAllActions];
    _sceneView.pointOfView = nil; _sceneView.scene = nil; _camera = nil; _orbit = nil;
    [_sceneView removeFromSuperview]; _sceneView = nil;
    [_upperLensBlur removeFromSuperview]; _upperLensBlur = nil;
    [_lowerLensBlur removeFromSuperview]; _lowerLensBlur = nil;
}
- (void)dealloc { [NSNotificationCenter.defaultCenter removeObserver:self]; [self retire]; }
@end
