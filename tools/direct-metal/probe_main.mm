#include "direct_metal_probe.h"
#include <iostream>
int main(int argc, char** argv) {
  @autoreleasepool {
    if (argc != 3) { std::cerr << "Usage: metal_probe libraries output\n"; return 2; }
    auto report = RunDirectMetalValidation([NSString stringWithUTF8String:argv[1]],
                                          [NSString stringWithUTF8String:argv[2]]);
    NSData* data = [NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted error:nil];
    std::cout << std::string(static_cast<const char*>(data.bytes), data.length) << '\n';
    return [report[@"passed"] boolValue] ? 0 : 1;
  }
}
