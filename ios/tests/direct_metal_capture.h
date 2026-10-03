#pragma once
#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>
NSDictionary* ReplayDirectMetalCaptures(NSString* libraries, NSString* captures, NSString* output);
bool PresentDirectMetalCapture(CAMetalLayer* layer, NSString* libraries, NSString* capture, NSString** failure);
