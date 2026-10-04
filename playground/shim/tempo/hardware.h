#pragma once
// Stand-in for chompi-tempo/code/src/hardware.h, swapped in by a clang VFS
// overlay (see the Makefile) so the firmware files stay untouched.
//
// The real header describes the whole board: encoders, LED drivers, shift
// registers, battery charger, MIDI ports. Of that, the audio engines only need
// the MIDI-out calls the arpeggiator makes, which are no-ops here.
#include "daisy_seed.h"
#include "hid/MidiEvent.h"  // NoteOn / NoteOff, as the real header gets them

#define NO_BATT false

using namespace daisy;

namespace chompi
{
// From temp_led_stuff.h, which granularDelay.h reaches through the real
// hardware.h. The rest of that file drives LED timers.
inline float color_xfade(float start, float end, float idx)
{
    return (1.f - idx) * start + idx * end;
}

class Hardware
{
  public:
    void queueMidiNote(int, int, int, int) {}
    void queueMidiTransport(int, bool) {}
    void queueMidiCC(int, int, int) {}
};
} // namespace chompi
