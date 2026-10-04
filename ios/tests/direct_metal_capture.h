#pragma once
#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>
NSDictionary* ReplayDirectMetalCaptures(NSString* libraries, NSString* captures, NSString* output);
bool PresentDirectMetalCapture(CAMetalLayer* layer, NSString* libraries, NSString* capture, NSString** failure);

#ifdef THEFT4_SHADER_BENCHMARK
NSDictionary* CompareDirectMetalCaptureShaders(NSString* baseline,NSString* candidate,NSString* captures,NSString* output);
#endif
