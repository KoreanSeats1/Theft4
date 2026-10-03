#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

// A capability check, not a model-name or memory-size approximation.
FOUNDATION_EXPORT BOOL Theft4DeviceNeedsBCTexturePreparation(void);
// Restrict the older native presentation path to BC-incompatible Apple GPU
// families before Apple 10; recent BC-incompatible GPUs keep their own path.
FOUNDATION_EXPORT BOOL Theft4DeviceNeedsOlderBCPresentation(void);

// Inventories source containers without reading or changing their contents.
// Individual textures inside RPF, IMG, and XTD files require a separate parser.
FOUNDATION_EXPORT NSDictionary<NSString *, id> * _Nullable
Theft4InventoryTextureSources(NSURL *gameDirectory, NSError * _Nullable * _Nullable error);

FOUNDATION_EXPORT BOOL Theft4SaveTextureSourceInventory(
    NSDictionary<NSString *, id> *inventory, NSURL *supportDirectory,
    NSError * _Nullable * _Nullable error);

NS_ASSUME_NONNULL_END
