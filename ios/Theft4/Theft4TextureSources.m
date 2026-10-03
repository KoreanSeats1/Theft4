#import "Theft4TextureSources.h"
#import <Metal/Metal.h>
#include "theft4_bc_compatibility.h"

static NSString *const Theft4TextureInventoryErrorDomain = @"Theft4TextureInventory";

BOOL Theft4DeviceNeedsBCTexturePreparation(void) {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    // If capability detection is unavailable, favor the compatibility path.
    return theft4_bc_needs_preparation(device &&
           [device respondsToSelector:@selector(supportsBCTextureCompression)] &&
           device.supportsBCTextureCompression);
}

BOOL Theft4DeviceNeedsOlderBCPresentation(void) {
    if (!Theft4DeviceNeedsBCTexturePreparation()) return NO;
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    return device && ![device supportsFamily:MTLGPUFamilyApple10];
}

static NSString *Theft4TextureSourceKind(NSString *extension) {
    if ([extension isEqualToString:@"xtd"] || [extension isEqualToString:@"wtd"])
        return @"texture-dictionary";
    if ([extension isEqualToString:@"rpf"] || [extension isEqualToString:@"img"])
        return @"archive";
    // Drawable and fragment resources may embed or reference textures. Their
    // contents must be inspected before labeling them conversion candidates.
    if ([@[@"xdr", @"wdr", @"xdd", @"wdd", @"xft", @"wft"] containsObject:extension])
        return @"graphics-resource";
    return nil;
}

NSDictionary<NSString *, id> *Theft4InventoryTextureSources(NSURL *gameDirectory,
                                                              NSError **error) {
    NSFileManager *files = NSFileManager.defaultManager;
    NSNumber *isDirectory = nil;
    if (![gameDirectory getResourceValue:&isDirectory forKey:NSURLIsDirectoryKey error:error] ||
        !isDirectory.boolValue) {
        if (error && !*error) *error = [NSError errorWithDomain:Theft4TextureInventoryErrorDomain
            code:1 userInfo:@{NSLocalizedDescriptionKey: @"The game directory is unavailable."}];
        return nil;
    }

    NSMutableArray<NSDictionary *> *sources = [NSMutableArray new];
    NSMutableDictionary<NSString *, NSNumber *> *extensionCounts = [NSMutableDictionary new];
    NSUInteger inspected = 0;
    __block NSError *scanError = nil;
    NSDirectoryEnumerator<NSURL *> *entries = [files enumeratorAtURL:gameDirectory
        includingPropertiesForKeys:@[NSURLIsDirectoryKey, NSURLIsRegularFileKey,
                                     NSURLIsSymbolicLinkKey, NSURLFileSizeKey]
        options:NSDirectoryEnumerationSkipsHiddenFiles
        errorHandler:^BOOL(NSURL *url, NSError *readError) {
            if (!scanError) scanError = readError;
            return NO;
        }];
    for (NSURL *url in entries) {
        if (++inspected > 500000) {
            if (error) *error = [NSError errorWithDomain:Theft4TextureInventoryErrorDomain
                code:2 userInfo:@{NSLocalizedDescriptionKey: @"The game directory has too many entries to inventory."}];
            return nil;
        }
        NSNumber *symlink = nil;
        NSNumber *regular = nil;
        if (![url getResourceValue:&symlink forKey:NSURLIsSymbolicLinkKey error:error] ||
            ![url getResourceValue:&regular forKey:NSURLIsRegularFileKey error:error]) return nil;
        if (symlink.boolValue) {
            [entries skipDescendants];
            continue;
        }
        if (!regular.boolValue) continue;
        NSString *extension = url.pathExtension.lowercaseString;
        if (!extension.length) extension = @"(none)";
        extensionCounts[extension] = @([extensionCounts[extension] unsignedIntegerValue] + 1);
        NSString *kind = Theft4TextureSourceKind(extension);
        if (!kind) continue;
        NSNumber *size = nil;
        if (![url getResourceValue:&size forKey:NSURLFileSizeKey error:error]) return nil;
        NSString *root = gameDirectory.path.stringByStandardizingPath;
        NSString *path = url.path.stringByStandardizingPath;
        if (![path hasPrefix:[root stringByAppendingString:@"/"]]) continue;
        [sources addObject:@{@"path": [path substringFromIndex:root.length + 1],
                             @"kind": kind, @"bytes": size ?: @0}];
    }
    if (scanError) {
        if (error) *error = scanError;
        return nil;
    }
    [sources sortUsingComparator:^NSComparisonResult(NSDictionary *a, NSDictionary *b) {
        return [a[@"path"] compare:b[@"path"] options:NSCaseInsensitiveSearch];
    }];
    NSUInteger dictionaries = 0, archives = 0, graphics = 0;
    for (NSDictionary *source in sources) {
        NSString *kind = source[@"kind"];
        if ([kind isEqualToString:@"texture-dictionary"]) ++dictionaries;
        else if ([kind isEqualToString:@"archive"]) ++archives;
        else ++graphics;
    }
    return @{@"schemaVersion": @1,
             @"scope": @"source-containers-only",
             @"note": @"This lists source files, not individual textures or completed conversions.",
             @"filesInspected": @(inspected),
             @"textureDictionaries": @(dictionaries),
             @"archives": @(archives),
             @"graphicsResources": @(graphics),
             @"extensionCounts": extensionCounts,
             @"sources": sources};
}

BOOL Theft4SaveTextureSourceInventory(NSDictionary<NSString *, id> *inventory,
                                      NSURL *supportDirectory, NSError **error) {
    if (!inventory || !supportDirectory) return NO;
    NSURL *directory = [supportDirectory URLByAppendingPathComponent:@"texture-preparation"
                                                          isDirectory:YES];
    if (![NSFileManager.defaultManager createDirectoryAtURL:directory
        withIntermediateDirectories:YES attributes:nil error:error]) return NO;
    NSData *data = [NSJSONSerialization dataWithJSONObject:inventory
        options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:error];
    if (!data) return NO;
    return [data writeToURL:[directory URLByAppendingPathComponent:@"source-inventory.json"]
                   options:NSDataWritingAtomic error:error];
}
