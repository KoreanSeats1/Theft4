#include "direct_metal_probe.h"
#include "direct_metal_capture.h"
#include <iostream>
int main(int argc, char** argv) {
  @autoreleasepool {
    if (argc != 3 && argc != 4) { std::cerr << "Usage: metal_probe libraries output [private-captures]\n"; return 2; }
    auto report = RunDirectMetalValidation([NSString stringWithUTF8String:argv[1]],
                                          [NSString stringWithUTF8String:argv[2]]);
    if(argc==4) {
      const bool graphics_passed=[report[@"passed"] boolValue];
      auto replay=ReplayDirectMetalCaptures([NSString stringWithUTF8String:argv[1]],
          [NSString stringWithUTF8String:argv[3]],[[NSString stringWithUTF8String:argv[2]] stringByAppendingPathComponent:@"GameDrawReplay"]);
      auto combined=[replay mutableCopy];combined[@"graphics_validation_passed"]=@(graphics_passed);
      combined[@"replay_passed"]=replay[@"passed"];
      combined[@"passed"]=@(graphics_passed&&[replay[@"passed"] boolValue]);report=combined;
    }
    NSData* data = [NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted error:nil];
    std::cout << std::string(static_cast<const char*>(data.bytes), data.length) << '\n';
    return [report[@"passed"] boolValue] ? 0 : 1;
  }
}
