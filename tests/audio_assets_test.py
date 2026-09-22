#!/usr/bin/env python3
"""Decode generated blocks independently, including Redux's predictor rounding."""
import importlib.util
import math
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

lowest = [100, 100]
for octave, length in enumerate([2688,1344,672,336,168,84,84,84,84,84]):
    cycles = 1 << max(0, octave-5)
    pcm = [round(16384*math.sin(2*math.pi*cycles*i/length)) for i in range(length)]
    data, reference = generator.encode(pcm)
    assert data[1]==4 and data[-15]==3
    assert decode(data,False)==reference
    for mode in range(2):
        output = decode(data,mode)
        power=sum(x*x for x in pcm)
        error=sum((x-y)**2 for x,y in zip(pcm,output))
        snr=10*math.log10(power/error)
        lowest[mode]=min(lowest[mode],snr)
        assert snr>23 and max(map(abs,output))<18000
        assert abs((output[0]-output[-1])-(pcm[0]-pcm[-1]))<700
print(f'PASS: ten repeatable ADPCM loops; minimum decoded SNR {lowest[0]:.2f} dB (rounded), {lowest[1]:.2f} dB (Redux)')
