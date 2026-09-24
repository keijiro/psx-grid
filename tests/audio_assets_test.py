#!/usr/bin/env python3
"""Decode generated blocks independently, including Redux's predictor rounding."""
import importlib.util
import math
import re
import sys
sys.dont_write_bytecode = True
from pathlib import Path
spec = importlib.util.spec_from_file_location('audio_generator', Path(__file__).resolve().parents[1]/'scripts/generate-audio.py')
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)
coefficients = [(0, 0), (60, 0), (115, -52), (98, -55), (122, -60)]

def decode(data, separate_rounding):
    previous, older = 0, 0
    loops = []
    for _ in range(3):
        output = []
        for offset in range(0, len(data), 16):
            shift, predictor = data[offset] & 15, data[offset] >> 4
            a, b = coefficients[predictor]
            for packed in data[offset+2:offset+16]:
                for raw in (packed & 15, packed >> 4):
                    nibble = raw if raw < 8 else raw-16
                    prediction = ((previous*a)>>6)+((older*b)>>6) if separate_rounding else (previous*a+older*b+32)>>6
                    sample = max(-32768,min(32767,(nibble << (12-shift))+prediction))
                    older, previous = previous, sample
                    output.append(sample)
        loops.append(output)
    assert loops[0] == loops[1] == loops[2], 'Decoder history leaks across loop entry'
    return loops[0]

peaks = [0, 0]
expected_data, expected_offsets = bytearray(), []
for bank in range(len(generator.LENGTHS)):
    for wave, name in enumerate(generator.WAVES):
        pcm = generator.wave_samples(wave, bank)
        data, reference = generator.encode(pcm)
        assert data == generator.encode(generator.wave_samples(wave, bank))[0]
        expected_offsets.append(len(expected_data))
        expected_data.extend(data)
        assert len(data) == len(pcm)//28*16 and data[0] >> 4 == 0
        assert all((data[i] & 15) <= 12 and data[i] >> 4 < 5 for i in range(0,len(data),16))
        assert data[1]==4 and data[-15]==3
        assert all(data[i+1]==0 for i in range(16, len(data)-16, 16))
        assert decode(data,False)==reference
        for mode in range(2):
            output = decode(data,mode)
            power = sum(x*x for x in pcm)
            error = sum((x-y)**2 for x,y in zip(pcm,output))
            snr = 10*math.log10(power/error) if error else math.inf
            peak = max(map(abs, output))
            peaks[mode] = max(peaks[mode], peak)
            # Discontinuities are intentional for saw, square and noise.
            # Report shape-relative boundary error for the later audio review.
            boundary = (output[0]-output[-1])-(pcm[0]-pcm[-1])
            print(f'C{bank} {name} decoder={mode}: SNR={snr:.2f} dB, peak={peak}, '
                  f'DC={sum(output)/len(output):.2f}, boundary error={boundary}')
            assert peak < 32768
            assert snr > 20
            # Saw/square have a sampling-dependent mean; compare DC with the
            # intended discrete waveform instead of assuming perfect symmetry.
            assert abs(sum(output)/len(output)-sum(pcm)/len(pcm)) < generator.SAMPLE_PEAK*0.025
            if wave == 0:
                assert snr > 23 and peak < 18000 and abs(boundary) < 700
for note in range(109):
    for sweep in range(-24,25):
        bank = generator.select_bank(note,sweep)
        for n in (note, max(0,min(108,note+sweep))):
            ideal = generator.pitch_value(bank,n)
            assert 1 <= round(ideal) <= 16383
            assert abs(1200*math.log2(round(ideal)/ideal)) < 2
print(f'PASS: 50 deterministic, repeatable loops; decoded peaks {peaks}')

# Check the bytes actually consumed by the backend, including alignment and
# padding, rather than only checking the encoder's in-memory return value.
header = (Path(__file__).resolve().parents[1]/'build/generated/wave_samples.h').read_text()
def values(name):
    body = re.search(r'\b'+name+r'\[\] = \{(.*?)\};',header,re.S)[1]
    return [int(x) for x in re.findall(r'\d+',body)]
assert values('wave_offsets') == expected_offsets
assert all(offset % 16 == 0 for offset in expected_offsets)
expected_data.extend(bytes((-len(expected_data)) % 64))
actual = b''.join(value.to_bytes(4,'little') for value in values('wave_data'))
assert actual == expected_data and len(actual) % 64 == 0
assert generator.SPU_ADDRESS % 64 == 0
assert generator.SPU_ADDRESS+len(actual) <= 512*1024
print(f'PASS: generated addresses, exact bytes and zero DMA padding; {len(actual)} SPU bytes, '
      f'12-pair decoded headroom peak {max(peaks)*12*generator.PAIR_GAIN/16384:.3f} / 32768')
