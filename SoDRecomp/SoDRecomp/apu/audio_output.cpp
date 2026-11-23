// Host audio output: an SDL3 stream that plays the 5.1 frames the game's XAudio2
// submits (sod::apu::PopFrame). The 5.1 frame is folded to stereo here, before SDL:
// handed six channels, Android's own downmix (AAudio, OEM dependent) all but drops the
// centre channel, where games put dialogue, so speech comes out faint and distant
// while the front-channel sounds and music stay loud. SDL converts the rate.
#include "stdafx.h"
#include "apu/audio.h"
#include "apu/audio_output.h"

#include <SDL3/SDL.h>

#include <algorithm>

namespace sod::apu
{
    namespace
    {
        constexpr uint32_t kOutputChannels = 2;

        // 5.1 -> stereo fold (ITU-style, the same weights ME1Recomp uses): centre and
        // surrounds at -3 dB, LFE dropped, then scaled by 1 / (1 + 0.707) so a full-scale
        // front plus centre doesn't clip. XAudio2's 5.1 order: FL, FR, FC, LFE, BL, BR.
        constexpr float kFoldCenter = 0.70710678f;
        constexpr float kFoldSurround = 0.70710678f;
        constexpr float kFoldScale = 0.58578644f;

        SDL_AudioStream* s_stream = nullptr;
        uint64_t s_underruns = 0;
        bool s_gameAudioStarted = false; // (the silence before the game's first frame isn't an underrun)
        FILE* s_dump = nullptr; // SOD_AUDIO_DUMP=<path.wav>: the game's 5.1 output, as 6-channel float WAV
        uint32_t s_dumpBytes = 0;

        void UpdateDumpHeader()
        {
            struct
            {
                char riff[4]; uint32_t riffSize; char wave[4];
                char fmt[4]; uint32_t fmtSize; uint16_t format, channels; uint32_t rate, byteRate;
                uint16_t blockAlign, bits;
                char data[4]; uint32_t dataSize;
            } header = { { 'R', 'I', 'F', 'F' }, 36 + s_dumpBytes, { 'W', 'A', 'V', 'E' }, { 'f', 'm', 't', ' ' }, 16,
                3, uint16_t(kFrameChannels), 48000, 48000 * kFrameChannels * 4, uint16_t(kFrameChannels * 4), 32,
                { 'd', 'a', 't', 'a' }, s_dumpBytes };
            fseek(s_dump, 0, SEEK_SET);
            fwrite(&header, sizeof(header), 1, s_dump);
            fseek(s_dump, 0, SEEK_END);
        }

        // Folds one planar 5.1 frame into interleaved stereo.
        void FoldToStereo(const float* planar, float* stereo)
        {
            const float* fl = planar + 0 * kFrameChannelSamples;
            const float* fr = planar + 1 * kFrameChannelSamples;
            const float* fc = planar + 2 * kFrameChannelSamples;
            const float* bl = planar + 4 * kFrameChannelSamples;
            const float* br = planar + 5 * kFrameChannelSamples;
            for (uint32_t s = 0; s < kFrameChannelSamples; s++)
            {
                const float mid = fc[s] * kFoldCenter;
                stereo[s * 2] = std::clamp((fl[s] + mid + bl[s] * kFoldSurround) * kFoldScale, -1.0f, 1.0f);
                stereo[s * 2 + 1] = std::clamp((fr[s] + mid + br[s] * kFoldSurround) * kFoldScale, -1.0f, 1.0f);
            }
        }

        void DumpFrame(const float* planar)
        {
            float interleaved[kFrameSampleCount];
            for (uint32_t s = 0; s < kFrameChannelSamples; s++)
            {
                for (uint32_t c = 0; c < kFrameChannels; c++)
                    interleaved[s * kFrameChannels + c] = planar[c * kFrameChannelSamples + s];
            }
            fwrite(interleaved, sizeof(interleaved), 1, s_dump);
            s_dumpBytes += uint32_t(sizeof(interleaved));
            if ((s_dumpBytes / sizeof(interleaved)) % 188 == 0) // ~1 s
                UpdateDumpHeader();
        }

        // Called by SDL's audio thread whenever the device needs more data.
        void SDLCALL FeedDevice(void*, SDL_AudioStream* stream, int additionalAmount, int)
        {
            constexpr int kOutputBytes = int(kFrameChannelSamples * kOutputChannels * sizeof(float));
            float planar[kFrameSampleCount];
            float stereo[kFrameChannelSamples * kOutputChannels];
            while (additionalAmount > 0)
            {
                if (PopFrame(planar))
                {
                    s_gameAudioStarted = true;
                    FoldToStereo(planar, stereo);
                }
                else
                {
                    // The game hasn't produced audio in time (or isn't running audio yet).
                    memset(planar, 0, sizeof(planar));
                    memset(stereo, 0, sizeof(stereo));
                    if (s_gameAudioStarted)
                    {
                        const uint64_t underruns = ++s_underruns;
                        if (underruns == 100 || underruns == 1000 || underruns % 10000 == 0)
                            LOGW("Audio output: game audio is not keeping up ({} underruns so far)", underruns);
                    }
                }
                SDL_PutAudioStreamData(stream, stereo, kOutputBytes);
                if (s_dump)
                    DumpFrame(planar);
                additionalAmount -= kOutputBytes;
            }
        }

        bool OpenStream()
        {
            const SDL_AudioSpec spec{ SDL_AUDIO_F32, int(kOutputChannels), 48000 };
            SDL_AudioStream* stream =
                SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, FeedDevice, nullptr);
            if (!stream)
                return false;
            if (const char* dump = getenv("SOD_AUDIO_DUMP"))
            {
                s_dump = fopen(dump, "wb");
                if (s_dump)
                    UpdateDumpHeader();
            }
            s_stream = stream;
            SDL_ResumeAudioStreamDevice(s_stream);
            LOGI("Audio output: {} via SDL {}, 5.1 folded to stereo", SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "?",
                SDL_GetVersion());
            return true;
        }
    }

    bool InitializeAudioOutput()
    {
#if defined(__ANDROID__)
        // AAudio's low-latency mode hands out an exclusive (MMAP) stream that Android may refuse or
        // take back at startup ("AAUDIO_ERROR_DISCONNECTED", "stream was probably stolen": a Dragon
        // Age player's Galaxy S23 Ultra started silent until a relaunch, same code). The game buffers
        // audio anyway, so the shared stream's few extra milliseconds don't matter.
        SDL_SetHint(SDL_HINT_ANDROID_LOW_LATENCY_AUDIO, "0");
#endif
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
            LOGW("Audio output: SDL audio init failed: {}", SDL_GetError());
            return false;
        }
        if (OpenStream())
            return true;
        LOGW("Audio output: can't open the playback device: {}; retrying in the background", SDL_GetError());
        // The device can be busy for a moment (another app's sound, a Bluetooth switch): keep trying
        // once a second for a minute instead of leaving the whole session silent.
        std::thread([] {
            for (int attempt = 1; attempt <= 60; attempt++)
            {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                if (OpenStream())
                {
                    LOGI("Audio output: playback device opened on retry {}", attempt);
                    return;
                }
            }
            LOGW("Audio output: gave up opening the playback device: {}", SDL_GetError());
        }).detach();
        return false;
    }
}
