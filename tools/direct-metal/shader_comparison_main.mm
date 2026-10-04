#include "direct_metal_capture.h"
#include <iostream>
int main(int argc,char** argv){
  if(argc!=5)return 2;
  @autoreleasepool {
    auto report=CompareDirectMetalCaptureShaders([NSString stringWithUTF8String:argv[1]],
      [NSString stringWithUTF8String:argv[2]],[NSString stringWithUTF8String:argv[3]],
      [NSString stringWithUTF8String:argv[4]]);
    std::cout<<"shader comparison passed="<<[report[@"passed"] boolValue]<<" cases="<<[report[@"cases"] count]<<"\n";
    return [report[@"passed"] boolValue]?0:1;
  }
}
