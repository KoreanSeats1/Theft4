#import <UIKit/UIKit.h>

// UIKit-only adapter; no XeniOS emulator, renderer or editor dependency.
@interface Theft4TouchControls : UIView
@property(nonatomic, getter=isActive) BOOL active;
- (void)releaseAllTouches;
@end
