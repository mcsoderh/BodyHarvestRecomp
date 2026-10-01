#ifndef __BODYHARVEST_AUDIO_H__
#define __BODYHARVEST_AUDIO_H__

#include <cstddef>
#include <cstdint>

namespace bodyharvest::audio {
    // Opens the SDL audio device. Returns false if no device is available.
    bool reset_audio(uint32_t output_freq);

    void queue_samples(int16_t* audio_data, size_t sample_count);
    size_t get_frames_remaining();
    void set_frequency(uint32_t freq);
}

#endif
