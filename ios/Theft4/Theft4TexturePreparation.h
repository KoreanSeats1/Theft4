#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN
@interface Theft4TexturePreparation : NSObject
@property(nonatomic, readonly) BOOL running;
+ (BOOL)isCompleteForGame:(NSURL *)game support:(NSURL *)support;
+ (BOOL)deletePreparedCacheAtSupport:(NSURL *)support error:(NSString * _Nullable * _Nullable)error;
- (void)startForGame:(NSURL *)game support:(NSURL *)support
           progress:(void (^)(NSDictionary *progress))progress
         completion:(void (^)(BOOL complete, NSDictionary *summary, NSString * _Nullable error))completion;
- (void)cancel;
@end
NS_ASSUME_NONNULL_END
