/*
 * audio_hw.h - PlayStation audio hardware services
 *
 * The Rust transport calls these serialized SPU and timer operations. The
 * timer callback enters Rust only after the transport is initialized.
 */

#ifndef AUDIO_HW_H
#define AUDIO_HW_H

#include "audio_sequencer.h"

/*
 * Services one timer tick after the hardware callback samples its clock.
 */
void audio_transport_service(AudioTime now);

/*
 * Initializes SPU resources and arms the transport timer after Rust setup.
 */
void audio_hw_init(void);
/*
 * Masks interrupts while the main thread changes shared transport state.
 */
void audio_hw_enter(void);
/*
 * Restores interrupts after a transport state change.
 */
void audio_hw_exit(void);
/*
 * Returns the extended hardware clock while service is excluded.
 */
AudioTime audio_hw_now(void);
/*
 * Samples the extended clock with interrupts excluded.
 */
AudioTime audio_hw_time(void);
/*
 * Reconfigures the SPU reverb network for the current score.
 * `score` must not be NULL.
 */
void audio_hw_reverb(const Score* score);
/*
 * Silences all hardware voices before restarting the Rust synthesizer.
 */
void audio_hw_restart(void);
/*
 * Silences the SPU and detaches the timer before BIOS card ownership.
 */
void audio_hw_card_stop(void);
/*
 * Restores the timer and reverb network after BIOS card ownership.
 * `score` must not be NULL.
 */
void audio_hw_card_resume(const Score* score);
/*
 * Installs one logical voice into its paired SPU channels.
 * `sound` must not be NULL.
 */
void audio_hw_start_voice(void* context, int slot, int bank,
                          const SoundSettings* sound);
/*
 * Writes the paired voice volumes when their cached values change.
 */
void audio_hw_volume(void* context, int slot, int a, int b);
/*
 * Writes the paired voice pitch when its cached value changes.
 */
void audio_hw_pitch(void* context, int slot, int value);
/*
 * Commits pending key changes and their reverb send mask.
 */
void audio_hw_flush(void* context, uint32_t starts, uint32_t stops,
                    uint32_t sends, uint32_t dispatch_peak);
/*
 * Reports whether both halves of a logical voice have started.
 */
int audio_hw_ready(void* context, int slot);
/*
 * Returns the hardware clock through the synthesizer callback ABI.
 */
AudioTime audio_hw_clock(void* context);

#endif // AUDIO_HW_H
