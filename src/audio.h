#ifndef AUDIO_H
#define AUDIO_H
#include "sequencer.h"
#define AUDIO_HARDWARE_VOICES 24
#define AUDIO_IDLE_MASK ((1u<<SEQUENCER_VOICES)-1)
// Even a full-scale decoded overshoot sums to only 3/8 scale for 12 pairs.
// Complementary gains keep equal-wave pairs within the same per-note budget.
#define AUDIO_LEVEL 512
// Driver slots and flush masks refer to logical pairs, not SPU channels.
// Register writes are injected so allocation, envelopes and stale gate-offs
// use exactly the same code on the console and in the host fixture.
typedef struct {
    void *context;
    void (*start)(void *context, int slot, int bank, SoundSettings sound);
    void (*volume)(void *context, int slot, int a, int b);
    void (*pitch)(void *context, int slot, int value);
    void (*flush)(void *context, uint32_t starts, uint32_t stops);
    // Optional: both voices have begun playback after the latest key-on.
    int (*ready)(void *context, int slot);
} AudioDriver;
typedef struct {
    AudioTime start, release_at, end, modulation_start;
    uint32_t generation;
    SoundSettings sound;
    int active, releasing, waiting, level, release_level, pitch, bank, base_pitch;
} AudioVoice;
typedef struct {
    AudioVoice voices[SEQUENCER_VOICES];
    AudioDriver driver;
    uint32_t starts, stops, steals, idle_mask;
    AudioTime allocation_time;
} Audio;
AudioTime audio_ms(int ms);
void audio_init(Audio *audio, AudioDriver driver);
NoteSink audio_sink(Audio *audio);
uint32_t audio_reverb_mask(const Audio *audio);
// Platform lifecycle and live publication. Call update regularly on the main
// thread, even without input, to publish edits coalesced behind a pending score.
void audio_platform_init(void);
AudioTime audio_platform_time(void);
void audio_platform_update(const Score *score, int connected, int start);
int audio_platform_playing(void);
uint32_t audio_platform_revision(void);
#endif
