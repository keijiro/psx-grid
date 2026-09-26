/*
 * audio_abi.c - C value-ABI adapters for the Rust voice synthesizer
 *
 * Implementation notes:
 *
 * MIPS C passes aggregate arguments and results differently from Rust.
 * These adapters keep value-only callbacks on the C side of the boundary.
 */

#include "audio/audio.h"

#include <stddef.h>

_Static_assert(sizeof(AudioVoice) == 104, "Rust AudioVoice ABI changed");
_Static_assert(offsetof(Audio, driver) == 1248, "Rust Audio ABI changed");

extern void rust_audio_init(Audio* audio, const AudioDriver* driver);
extern uint32_t rust_audio_on(void* context, AudioTime now, int pitch,
                              const SoundSettings* sound);
extern void rust_audio_off(void* context, AudioTime now, uint32_t token);
extern void rust_audio_stop(void* context, AudioTime now);
extern void rust_audio_advance(void* context, AudioTime now);

static uint32_t on(void* context, AudioTime now, int pitch,
                   SoundSettings sound)
{
    return rust_audio_on(context, now, pitch, &sound);
}

void audio_driver_start(const AudioDriver* driver, int slot, int bank,
                        const SoundSettings* sound)
{
    driver->start(driver->context, slot, bank, *sound);
}

void audio_init(Audio* audio, AudioDriver driver)
{
    rust_audio_init(audio, &driver);
}

NoteSink audio_sink(Audio* audio)
{
    return (NoteSink){audio, on, rust_audio_off, rust_audio_stop,
                      rust_audio_advance};
}
