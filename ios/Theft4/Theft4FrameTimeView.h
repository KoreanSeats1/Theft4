#import <UIKit/UIKit.h>
#include "theft4_metal_presenter.h"

@interface Theft4FrameTimeView : UIView
- (void)updateWithSnapshot:(const theft4_frame_time_snapshot *)snapshot;
- (void)setCaptureRequested:(BOOL)requested;
- (void)setCaptureCompleted:(BOOL)success;
@end
