#!/usr/bin/env python3
"""Deterministic octave-root SPU ADPCM loops and tuning/error measurements."""
import math
import struct
import sys
from pathlib import Path

COEFFICIENTS = [(0, 0), (60, 0), (115, -52), (98, -55), (122, -60)]

def encode(pcm):
    history = [0, 0]
    data, decoded = bytearray(), []
    for offset in range(0, len(pcm), 28):
        best = None
        # Reset prediction at each loop entry. This makes every traversal and
        # a fresh key-on decode identically, including the first 28 samples.
        for predictor in ([0] if offset == 0 else range(5)):
            a, b = COEFFICIENTS[predictor]
            for shift in range(13):
                h1, h2 = history
                values, output, error = [], [], 0
                scale = 1 << (12-shift)
                for target in pcm[offset:offset+28]:
                    prediction = (h1*a+h2*b+32)//64
                    nibble = max(-8, min(7, round((target-prediction)/scale)))
                    sample = max(-32768, min(32767, prediction+nibble*scale))
                    values.append(nibble & 15)
                    output.append(sample)
                    # The reset block fixes the next traversal's first sample, so
                    # the terminal sample also controls the loop junction.
                    # Weight it as 16 samples: C9 sine's boundary error falls
                    # from 991 to 103 while its shape SNR stays above 25 dB.
                    weight = 16 if offset+28 == len(pcm) and len(values) == 28 else 1
                    error += weight*(sample-target)**2
                    h2, h1 = h1, sample
                candidate = (error, predictor, shift, values, output, [h1, h2])
                if best is None or error < best[0]:
                    best = candidate
        _, predictor, shift, values, output, history = best
        flags = 4 if offset == 0 else 0
        if offset+28 == len(pcm):
            flags |= 3
        data.extend([predictor*16+shift, flags])
        data.extend(values[i] | values[i+1]*16 for i in range(0, 28, 2))
        decoded.extend(output)
    return data, decoded

LENGTHS = [2688, 1344, 672, 336, 168, 84, 84, 84, 84, 84]
WAVES = ['Sine', 'Triangle', 'Saw', 'Square', 'Noise']
SPU_ADDRESS = 0x1000
SAMPLE_PEAK = 14336
PAIR_GAIN = 0x3fff


def wave_samples(wave, bank):
    length, cycles = LENGTHS[bank], 1 << max(0, bank-5)
    # Retain the demo's seeded 56-point noise cycle in every root bank.
    # Periodic interpolation shares the tonal waves' phase/cycle convention.
    state, noise = 0x6d2b79f5, []
    for _ in range(56):
        state ^= (state << 13) & 0xffffffff
        state ^= state >> 17
        state ^= (state << 5) & 0xffffffff
        noise.append((state & 65535)*2*SAMPLE_PEAK//65535-SAMPLE_PEAK)
    mean = round(sum(noise)/56)
    noise = [x-mean for x in noise]
    peak = max(map(abs, noise))
    noise = [int(x*SAMPLE_PEAK/peak) for x in noise]
    output = []
    for i in range(length):
        phase = (cycles*i % length)/length
        triangle = (phase+0.25) % 1
        position = phase*56
        index = int(position)
        value = [math.sin(2*math.pi*phase),
                 -1+4*triangle if triangle < 0.5 else 3-4*triangle,
                 -1+2*phase, 1 if phase < 0.5 else -1,
                 (noise[index]+(noise[(index+1)%56]-noise[index])*(position-index))/SAMPLE_PEAK][wave]
        output.append(round(SAMPLE_PEAK*value))
    if wave == 4:
        # Resampling the short noise cycle can introduce a new DC component.
        mean = sum(output)/length
        peak = max(abs(x-mean) for x in output)
        output = [round((x-mean)*SAMPLE_PEAK/peak) for x in output]
    return output


def pitch_value(bank, note):
    return 440*2**((note-57)/12)*LENGTHS[bank]/(1 << max(0, bank-5))/44100*4096


def select_bank(note, sweep):
    highest = min(108, max(note, note+sweep))
    return next(bank for bank in range(len(LENGTHS)) if pitch_value(bank, highest) <= 16383)


def array(name, typ, values):
    return f'static const {typ} {name}[] = {{\n'+''.join('    '+','.join(str(x) for x in values[i:i+12])+',\n' for i in range(0,len(values),12))+'};\n'


def rust_array(name, typ, values):
    descriptions = {
        'PITCH': 'Q8 SPU pitch values for ten banks and 109 semitones.',
        'SNAP': 'Q16 decay sampled at 1/1024 intervals.',
        'BANKS': 'Root bank for each note and signed sweep from -24 to 24.',
    }
    return f'/// {descriptions[name]}\n#[rustfmt::skip]\npub(super) static {name}: [{typ}; {len(values)}] = [\n'+''.join('    '+', '.join(str(x) for x in values[i:i+6])+',\n' for i in range(0,len(values),6))+'];\n'


def generate(path):
    data, roots, report = bytearray(), [], []
    peak = 0
    for bank, length in enumerate(LENGTHS):
        for wave, name in enumerate(WAVES):
            pcm = wave_samples(wave, bank)
            encoded, decoded = encode(pcm)
            roots.append(len(data))
            data.extend(encoded)
            peak = max(peak, max(map(abs, decoded)))
            error = sum((a-b)**2 for a,b in zip(pcm, decoded))
            snr = 10*math.log10(sum(x*x for x in pcm)/error) if error else math.inf
            report.append(f'C{bank} {name}: address {SPU_ADDRESS+roots[-1]:#x}, {length} samples, '
                          f'SNR {snr:.2f} dB, peak {max(map(abs, decoded))}, DC {sum(decoded)/length:.3f}, '
                          f'loop delta {decoded[0]-decoded[-1]}, ideal {pcm[0]-pcm[-1]}')
    # The sweep is monotonic in frequency. Selecting the longest root that
    # fits its upper endpoint maximizes register resolution throughout it.
    # Q8 pitch tables interpolate in semitone space (at most 0.723 cents
    # curvature error); report quantization separately at both endpoints.
    for note in range(109):
        for sweep in range(-24, 25):
            bank = select_bank(note, sweep)
            endpoints = [pitch_value(bank, n) for n in (note, max(0, min(108, note+sweep)))]
            assert all(1 <= round(v) <= 16383 for v in endpoints)
            quantization = max(abs(1200*math.log2(round(v)/v)) for v in endpoints)
            # Half-register quantization at the lowest pitch bounds the whole
            # trajectory, including frequencies between written semitones.
            bound = -1200*math.log2(1-0.5/min(endpoints))
            # Reserve 0.4 cents for Q16 time/Snap and Q12 note truncation:
            # the steepest 24-semitone onset contributes 19200/65536 cents
            # from time quantization alone. Decoder quality is separate.
            assert bound+0.723+0.4 < 2
            report.append(f'note {note}, sweep {sweep:+}: bank {bank}, registers '
                          f'{min(endpoints):.3f}..{max(endpoints):.3f}, endpoint quantization '
                          f'{quantization:.4f} cents, trajectory bound {bound:.4f} cents')
    raw_size = len(data)
    # SDK DMA rounds to 64 bytes; padding belongs to the asset, not its caller.
    data.extend(bytes((-len(data)) % 64))
    assert SPU_ADDRESS+len(data) <= 512*1024
    report.append(f'SPU bytes {raw_size}, DMA bytes {len(data)}, address {SPU_ADDRESS:#x}, end {SPU_ADDRESS+len(data):#x}')
    report.append(f'Decoded peak {peak}; 12 coherent pairs at gain {PAIR_GAIN}: {peak*12*PAIR_GAIN/16384:.1f} / 32768')
    prefix = '// Generated by scripts/generate-audio.py.\n#include <stdint.h>\n'
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(prefix+f'#define WAVE_SPU_ADDRESS {SPU_ADDRESS}u\n'+
                    array('wave_data','uint32_t',struct.unpack('<'+'I'*(len(data)//4),data))+
                    array('wave_offsets','uint16_t',roots))
    pitches = [round(pitch_value(bank, note)*256) for bank in range(10) for note in range(109)]
    snap = [round(65536*(math.exp(-8*i/1024)-math.exp(-8))/(1-math.exp(-8))) for i in range(1025)]
    banks = [select_bank(note, sweep) for note in range(109) for sweep in range(-24,25)]
    path.with_name('audio_tables.h').write_text(prefix+array('audio_pitch','uint32_t',pitches)+
                    array('audio_snap','uint32_t',snap)+
                    array('audio_banks','uint8_t',banks))
    rust_path = Path(__file__).resolve().parents[1] / 'rust/src/audio_tables.rs'
    rust_path.write_text('//! Fixed-point control tables generated from the shared audio assets.\n' +
                         rust_array('PITCH', 'u32', pitches) +
                         rust_array('SNAP', 'u32', snap) +
                         rust_array('BANKS', 'u8', banks))
    path.with_suffix('.txt').write_text('\n'.join(report)+'\n')


if __name__ == '__main__':
    generate(Path(sys.argv[1]))
