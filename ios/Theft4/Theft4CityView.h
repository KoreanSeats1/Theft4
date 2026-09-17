#import <UIKit/UIKit.h>

// Decorative, self-contained launcher renderer. Never accesses game resources.
@interface Theft4CityView : UIView
- (void)setActive:(BOOL)active;
- (void)retire;
@end
