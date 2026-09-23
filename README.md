# psx-grid

A PlayStation gamepad editor and wavetable sequencer for Jacquard's Score Plane.
It starts with an empty 128 x 64-cell plane. START plays at an adjustable tempo
(initially 120 BPM), with eight shared sound channels, 12-note polyphony, gates, branches, and
channel-local relative amplitude Attack/Release locks.
Each note mixes two selectable waveforms with an independent mix envelope
and signed pitch sweep. Regular lanes select a channel; branches inherit it.
Each channel controls its reverb send into one shared Size/Amount effect.
Committed score edits are published during playback without restarting the sequence.

See [usage](docs/usage.md), [development](docs/development.md), and the
[validation record](docs/validation.md). The main menu includes numbered
memory-card Save/Load and a one-block score budget. This storage implementation
passes host verification and direct-SIO emulator persistence checks. The default
BIOS backend still fails card discovery in the pinned emulator; backend selection,
hardware checks, and listening remain incomplete. See the
[storage design](docs/score-storage-design.md).
