#pragma once
#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>
NSDictionary* RunDirectMetalValidation(NSString* libraries, NSString* output);
bool PresentDirectMetalPreview(CAMetalLayer* layer, NSString* libraries, NSString** failure);
