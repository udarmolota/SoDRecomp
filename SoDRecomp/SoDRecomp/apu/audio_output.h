#pragma once

namespace sod::apu
{
    // Opens the default playback device and starts pulling the game's audio frames.
    // Call before any other SDL initialisation on other threads.
    bool InitializeAudioOutput();
}
