/*
 * audio_abi.c - C value-ABI adapters for the Rust voice synthesizer
 *
 * Implementation notes:
 *
 * MIPS C passes aggregate arguments and results differently from Rust.
 * The note sink adapter keeps its aggregate input on the C side of the
 * boundary. Rust owns the synthesizer and its callback table.
 */

#include "audio/audio_abi.h"

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

NoteSink audio_sink(Audio* audio)
{
    return (NoteSink){audio, on, rust_audio_off, rust_audio_stop,
                      rust_audio_advance};
}
