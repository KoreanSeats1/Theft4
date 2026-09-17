#include "theft4_output_policy.h"
#include <rex/ui/guest_output_frame_sequence.h>

#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <utility>

int main() {
    const auto baseline = theft4_output_policy_for_enhanced(false);
    const auto enhanced = theft4_output_policy_for_enhanced(true);
    assert(baseline.render_width == 1280 && baseline.render_height == 720);
    assert(baseline.output_width == 1280 && baseline.output_height == 720);
    assert(!baseline.fsr1);
    assert(enhanced.render_width == baseline.render_width);
    assert(enhanced.render_height == baseline.render_height);
    assert(enhanced.output_width == 1920 && enhanced.output_height == 1080);
    assert(enhanced.fsr1);
    // FSR Quality uses a 1.5x per-axis ratio in the existing native hooks.
    assert(enhanced.output_width * 2 == enhanced.render_width * 3);
    assert(enhanced.output_height * 2 == enhanced.render_height * 3);
    assert(enhanced.output_width * 9 == enhanced.output_height * 16);
    assert(theft4_output_policy_for_enhanced(false).output_width == 1280);
    const auto boost = theft4_output_policy_for_mode(THEFT4_OUTPUT_FSR_BOOST, 2752, 2064);
    assert(boost.output_width == 2752 && boost.output_height == 1548);
    assert(boost.video_width == 1920 && boost.video_height == 1080);
    assert(boost.video_width * 2 == boost.render_width * 3);
    assert(boost.video_height * 2 == boost.render_height * 3);
    for (const auto size : {std::pair<unsigned,unsigned>{0,0}, {1366,1024},
                            {2064,2752}, {2420,1668}, {7680,4320}}) {
        const auto p = theft4_output_policy_for_mode(THEFT4_OUTPUT_FSR_BOOST,size.first,size.second);
        assert(p.output_width * 9 == p.output_height * 16);
        assert(p.output_width >= 1920 && p.output_width <= 3840);
        assert(p.render_width == 1280 && p.render_height == 720 && p.fsr1);
        assert(p.video_width == 1920 && p.video_height == 1080);
        if (size.first >= 1920 && size.second >= 1080) {
            assert(p.output_width <= size.first && p.output_height <= size.second);
        }
    }
    assert(theft4_output_policy_for_mode(THEFT4_OUTPUT_FSR_BOOST,0,0).output_width == 1920);

    rex::ui::GuestOutputContentSequence content;
    rex::ui::GuestOutputFrameCounter counter;
    assert(!counter.NotePresented(0, true));
    const auto first = content.Publish(true);
    assert(first == 1);
    // Includes the first content, even when the underlying image version is 0.
    assert(counter.NotePresented(first, true));
    assert(!counter.NotePresented(first, true));
    const auto second = content.Publish(true);  // same image may be reused
    assert(second != first);
    assert(!counter.NotePresented(second, false));
    assert(counter.NotePresented(second, true));  // failed present can retry
    assert(!counter.NotePresented(first, true));  // stale content cannot inflate FPS
    assert(counter.count() == 2);

    const auto dropped = content.Publish(true);
    const auto newest = content.Publish(true);
    assert(counter.NotePresented(newest, true));
    assert(!counter.NotePresented(dropped, true));
    assert(counter.count() == 3);  // don't add the gap in sequence numbers
    assert(content.Publish(false) == 0);  // blank output is not a game frame
    assert(!counter.NotePresented(0, true));
    assert(content.Publish(true) == newest + 1);

    rex::ui::GuestOutputContentSequence rotation;
    rex::ui::GuestOutputFrameCounter rotating_counter;
    for (unsigned frame = 0; frame < 30; ++frame) {
        // Reusing allocation IDs 0,1,2 has no bearing on content identity.
        const auto sequence = rotation.Publish(true);
        assert(rotating_counter.NotePresented(sequence, true));
        assert(!rotating_counter.NotePresented(sequence, true));
    }
    assert(rotating_counter.count() == 30);
    puts("720p/1080p/native-fit Boost policy and unique-publication FPS tests passed");
}
