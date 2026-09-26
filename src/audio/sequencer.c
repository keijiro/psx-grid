/*
 * sequencer.c - C entry point for Rust score traversal
 *
 * Implementation notes:
 *
 * MIPS C and Rust pass aggregate values differently. This entry point owns
 * the by-value sink while Rust performs traversal with pointer callbacks.
 */

#include "audio/sequencer.h"

extern void rust_sequencer_start(Sequencer* seq, const Score* score,
                                 const NoteSink* sink, AudioTime now);

void sequencer_start(Sequencer* seq, const Score* score, NoteSink sink,
                     AudioTime now)
{
    rust_sequencer_start(seq, score, &sink, now);
}
