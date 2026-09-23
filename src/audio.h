#ifndef AUDIO_H
#define AUDIO_H
#include "sequencer.h"
#define AUDIO_LEVEL 512
// Register writes are injected so allocation, envelopes and stale gate-offs
// use exactly the same code on the console and in the host fixture.
typedef struct {
    void *context;
    void (*start)(void *, int, int);
    void (*volume)(void *, int, int);
    void (*flush)(void *, uint32_t, uint32_t);
} AudioDriver;
typedef struct {
    AudioTime start, release_at, end;
    uint32_t generation;
    SoundSettings sound;
    int active, releasing, level, release_level, pitch;
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
// Platform lifecycle and live publication. Call update regularly on the main
// thread, even without input, to publish edits coalesced behind a pending score.
void audio_platform_init(void);
AudioTime audio_platform_time(void);
void audio_platform_update(const Score *score, int connected, int start);
int audio_platform_playing(void);
uint32_t audio_platform_revision(void);
#endif
