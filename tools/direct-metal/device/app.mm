#import <UIKit/UIKit.h>
#include "direct_metal_probe.h"
@interface MetalLabPreview : UIView
@end
@implementation MetalLabPreview
+ (Class)layerClass { return CAMetalLayer.class; }
@end
@interface MetalLabDelegate : UIResponder <UIApplicationDelegate>
@property(strong,nonatomic) UIWindow* window;
@property(strong,nonatomic) UITextView* text;
@property(strong,nonatomic) MetalLabPreview* preview;
@end
@implementation MetalLabDelegate
- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)options {
  (void)application;(void)options;
  self.window=[[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
  UIViewController* vc=[UIViewController new];vc.view.backgroundColor=[UIColor colorWithRed:0.12 green:0.06 blue:0.20 alpha:1];
  self.text=[UITextView new];self.text.editable=NO;self.text.backgroundColor=UIColor.clearColor;
  self.text.textColor=UIColor.whiteColor;self.text.font=[UIFont monospacedSystemFontOfSize:17 weight:UIFontWeightRegular];
  self.text.translatesAutoresizingMaskIntoConstraints=NO;
  self.text.text=@"Theft4 Metal Lab\n\nRunning graphics checks…\n\nThis separate test app does not launch the game or change its files.";
  self.preview=[MetalLabPreview new];self.preview.translatesAutoresizingMaskIntoConstraints=NO;
  self.preview.layer.contentsGravity=kCAGravityResizeAspect;
  [vc.view addSubview:self.text];[vc.view addSubview:self.preview];
  [NSLayoutConstraint activateConstraints:@[
    [self.text.topAnchor constraintEqualToAnchor:vc.view.safeAreaLayoutGuide.topAnchor constant:20],
    [self.text.leadingAnchor constraintEqualToAnchor:vc.view.leadingAnchor constant:24],
    [self.text.trailingAnchor constraintEqualToAnchor:vc.view.trailingAnchor constant:-24],
    [self.text.bottomAnchor constraintEqualToAnchor:self.preview.topAnchor constant:-16],
    [self.preview.leadingAnchor constraintEqualToAnchor:vc.view.leadingAnchor constant:24],
    [self.preview.trailingAnchor constraintEqualToAnchor:vc.view.trailingAnchor constant:-24],
    [self.preview.heightAnchor constraintEqualToConstant:180],
    [self.preview.bottomAnchor constraintEqualToAnchor:vc.view.safeAreaLayoutGuide.bottomAnchor constant:-20]]];
  self.window.rootViewController=vc;[self.window makeKeyAndVisible];
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
    @autoreleasepool {
      NSString* libraries=[NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"MetalShaders"];
      NSString* output=[NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject
          stringByAppendingPathComponent:@"MetalValidation"];
      NSDictionary* report=RunDirectMetalValidation(libraries,output);
      NSMutableString* text=[NSMutableString stringWithFormat:@"Theft4 Metal Lab\n\n%@\n%@\nBC textures: %@\n\n",
        [report[@"passed"] boolValue] ? @"All graphics checks passed" : @"A graphics check failed",
        report[@"device"],[report[@"supports_bc"] boolValue] ? @"Supported" : @"Uses prepared ASTC"];
      for(NSDictionary* result in report[@"cases"])
        [text appendFormat:@"%@ %@\n",[result[@"passed"] boolValue] ? @"✓" : @"✕",result[@"case"]];
      if(![report[@"passed"] boolValue])[text appendFormat:@"\n%@",report[@"failure"]];
      [text appendString:@"\nThese checks render with game shaders directly through Metal. The game app remains separate.\n\nReport: Files → Theft4 Metal Lab → MetalValidation"];
      dispatch_async(dispatch_get_main_queue(),^{
        NSString* failure=nil;
        bool shown=PresentDirectMetalPreview((CAMetalLayer*)self.preview.layer,libraries,&failure);
        self.text.text=[text stringByAppendingFormat:@"\nDirect Metal presentation: %@%@",
            shown ? @"Passed" : @"Failed",failure ? [@" — " stringByAppendingString:failure] : @""];
        NSMutableDictionary* final=[report mutableCopy];final[@"onscreen_metal_presentation"]=@(shown);
        final[@"app_build"]=[NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleVersion"];
        final[@"presentation_failure"]=failure ?: @"";
        final[@"passed"]=@([report[@"passed"] boolValue] && shown);
        NSData* json=[NSJSONSerialization dataWithJSONObject:final options:NSJSONWritingPrettyPrinted error:nil];
        [json writeToFile:[output stringByAppendingPathComponent:@"DIRECT_METAL_VALIDATION.json"] atomically:YES];
      });
    }
  });
  return YES;
}
@end
int main(int argc,char** argv){@autoreleasepool{return UIApplicationMain(argc,argv,nil,NSStringFromClass(MetalLabDelegate.class));}}
