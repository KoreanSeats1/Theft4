#include <rex/graphics/gta4_native/surface_view.h>
#include "graphics/gta4_native/native_surface_storage.h"
#include <array>
#include <cassert>
#include <cstdio>

using namespace rex::graphics::gta4_native;
static GuestSurfaceView view(uint32_t width,uint32_t height,bool multisample=false) {
    SurfaceDescriptor d{};
    d.handle=1;d.width=width;d.height=height;
    // Actual M5 failure: shared placement, 1x full scene / 4x half-size alias.
    d.base=multisample?0x00420028u:0x00800050u;
    d.address=multisample?0x00030001u:0x03F30001u;
    d.sample_type=DecodeSurfaceSampleType(d.base);
    GuestSurfaceView v{};assert(DecodeGuestSurfaceView(d,false,v));return v;
}
int main() {
    unsigned cases=0;
    // Include the observed 2421x1668 scene and its 605x417 bloom intermediate,
    // even original 16:9 modes, odd phone modes and both axes odd.
    for(auto size: {std::array{2421u,1668u}, {605u,417u}, {1920u,1080u},
                   {2560u,1440u}, {2736u,1260u}, {2556u,1179u}, {1729u,1085u}}) {
        const auto full=view(size[0],size[1]);
        const auto half=view(size[0]/2,size[1]/2,true);
        assert(NativeResolveSameRowLayout(GetGuestPlacementKey(full),half));
        assert(NativeResolveContains(full,half));
        assert((GetGuestPlacementKey(full)==GetGuestPlacementKey(half))==
               (!(size[0]%2)&&!(size[1]%2)));
        assert(NativeResolveCoordinate(size[0]/2,2,full.sample_width,size[0],true)==
               size[0]-size[0]%2);
        assert(NativeResolveCoordinate(size[1]/2,2,full.sample_height,size[1],true)==
               size[1]-size[1]%2);
        // Ceiling the alias would request uninitialized storage at an odd edge.
        if(size[0]%2||size[1]%2)
            assert(!NativeResolveContains(full,view((size[0]+1)/2,(size[1]+1)/2,true)));
        auto narrow=full; narrow.sample_width=half.sample_width-1;
        // This newer writer must remain a candidate, then fail containment.
        assert(NativeResolveSameRowLayout(GetGuestPlacementKey(narrow),half));
        assert(!NativeResolveContains(narrow,half));
        auto short_view=full;short_view.sample_height=half.sample_height-1;
        assert(NativeResolveSameRowLayout(GetGuestPlacementKey(short_view),half));
        assert(!NativeResolveContains(short_view,half));
        auto incompatible=half;incompatible.sample_pitch++;
        assert(!NativeResolveSameRowLayout(full,incompatible));
        assert(!NativeResolveContains(full,incompatible));
        incompatible=half;incompatible.placement_base_tiles++;
        assert(!NativeResolveContains(full,incompatible));
        incompatible=half;incompatible.depth=true;
        assert(!NativeResolveContains(full,incompatible));
        incompatible=half;incompatible.sample_width=0;
        assert(!NativeResolveContains(full,incompatible));
        ++cases;
    }
    std::printf("Native resolve: %u real/odd/even extents; exact placement keys, cropped edges and unsafe-source rejection passed\n",cases);
}
