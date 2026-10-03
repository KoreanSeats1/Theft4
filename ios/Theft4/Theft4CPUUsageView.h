#import <UIKit/UIKit.h>

// Main-thread controller API. Samples once per second only while visible and
// active. Device core bars include other processes; thread bars are this app.
// Delivered CSV rows use publication-capture columns; callback runs on main.
@interface Theft4CPUUsageView : UIView
@property(nonatomic, copy) void (^sampleLogHandler)(NSString *csvRows);
- (void)setMonitoringActive:(BOOL)active;
@end
