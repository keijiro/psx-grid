#!/usr/bin/env python3
"""Run the bounded SPU/clock fixture with the pinned emulator, without its UI."""
import math
import os
import re
from pathlib import Path
import subprocess
import sys
root = Path(__file__).resolve().parents[1]
pair_gain = 0x4000//12
configuration = sys.argv[1] if len(sys.argv)>1 else 'debug'
if configuration not in ('debug', 'release'):
    raise SystemExit('Usage: test-audio-emulator.py [debug|release]')
exe = root / 'build' / configuration / 'audio-fixture.exe'
if not exe.exists():
    raise SystemExit(f'Configure {configuration} with -DAUDIO_FIXTURE=ON and build first')
output = root / 'build/validation'
output.mkdir(parents=True, exist_ok=True)
data = output / ('emulator-'+configuration)
data.mkdir(exist_ok=True)
emulator = os.environ.get('PCSX_REDUX', str(root / '.local/PCSX-Redux.app/Contents/MacOS/PCSX-Redux'))
bios = os.environ.get('PCSX_REDUX_BIOS', str(root / '.local/PCSX-Redux.app/Contents/Resources/share/pcsx-redux/resources/openbios.bin'))
command = [emulator, '-portable', str(data), '-no-ui', '-no-gui-log', '-testmode', '-stdout', '-interpreter', '-bios', bios, '-exe', str(exe), '-run']
result = subprocess.run(command, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
log = result.stdout.decode(errors='replace')
(output / f'audio-{configuration}-emulator.log').write_text(log)
for line in log.splitlines():
    if line.startswith(('AUDIO ', 'CAPTURE ', 'ENVELOPE ', 'DISPATCH ', 'LIVE ', 'WAVE ', 'SYNTH ', 'TRANSIENT ', 'TEMPO ', 'REVERB ', 'CHANNEL ', 'RAM ')):
        print(line)
if result.returncode or 'AUDIO FIXTURE COMPLETE' not in log:
    raise SystemExit(f'Fixture failed: exit {result.returncode}; see {output}')

# Check device-path measurements as well as successful process completion.
# All thresholds are in the hardware clock, independent of host execution speed.
def fields(prefix):
    match = re.search(r'^'+re.escape(prefix)+r': (.*)$', log, re.MULTILINE)
    assert match, f'Missing measurement: {prefix}'
    return {key:int(value) for key,value in re.findall(r'(\w+)=(\d+)', match[1])}
for phase in ('C4 loop', '12-note chord', 'live pitch'):
    timing, dispatch = fields('AUDIO '+phase), fields('DISPATCH '+phase)
    assert timing['skipped']==0 and timing['overloads']==0, (phase, timing)
    assert 0 < dispatch['peak'] <= 4233 and timing['interval'] <= 4233, (phase, dispatch, timing)
chord = fields('DISPATCH 12-note chord')
assert chord['count']==12 and chord['span']==0, chord
loop = fields('DISPATCH C4 loop')
assert abs(loop['span']-(loop['count']-1)*529200) <= 8467, loop
for phase in ('stopped', 'final stop', 'pending stop', 'pending disconnect'):
    state = fields('AUDIO '+phase)
    # ADSR readback can lag in Redux's separate SPU thread. Every left/right
    # volume register at zero establishes silence independently of that value.
    assert state['vol']==state['volumes']==state['send']==0, state
for pitch in (0,4,9):
    match = re.search(r'CAPTURE C'+str(pitch)+r' peak=(\d+)', log)
    assert match and 0 < int(match[1]) < 32767
envelopes = re.findall(r'ENVELOPE request=(\d+) attack_ticks=(\d+) release_ticks=(\d+)', log)
assert {int(entry[0]) for entry in envelopes} == {0,1,5,100}
for request,attack,release in envelopes:
    expected = int(request)*4233600//1000
    assert abs(int(attack)-expected) <= 4233 and abs(int(release)-expected) <= 4233
stress = fields('AUDIO 4096 tiles')
assert stress['cost'] < 65536 and stress['interval'] < 65536, stress
assert stress['skipped']>0 and stress['overloads']>0, stress
for phase,count in (('live pitch',25),('live 4096 tiles',32)):
    live=fields('LIVE '+phase)
    assert live['edits']==live['adopted']==count and live['playing']==1, live
    timing=fields('AUDIO '+phase)
    assert timing['cost']<65536 and timing['interval']<65536, timing
registers=fields('LIVE pitch registers')
assert registers['before']>0 and registers['after']>0 and registers['before']!=registers['after'], registers
coalesced=fields('LIVE coalesced')
assert coalesced['pending']==coalesced['deferred']==coalesced['adopted']==coalesced['playing']==1, coalesced
for phase in ('pending stop','pending disconnect'):
    state=fields('LIVE '+phase)
    assert state['pending']==state['unchanged']==1 and state['playing']==0, state
waves=re.findall(r'WAVE wave=(\d+) peak=(\d+) pairs=(\d+) bound=(\d+)',log)
assert {int(row[0]) for row in waves}==set(range(5)), waves
for wave,peak,pairs,bound in waves:
    assert int(pairs)==12 and 0<int(peak)<32767 and 0<int(bound)<32768, (wave,peak,pairs,bound)
for sign in (-1,1):
    rows=re.findall(r'SYNTH sign='+str(sign)+r' ms=(\d+) ticks=(\d+) pitch=(\d+) a=(\d+) b=(\d+) pairs=(\d+)',log)
    assert len(rows)==7, rows
    base=int(rows[-1][2])
    # C4 uses a 672-sample bank for a downward sweep and a 168-sample
    # bank for an upward sweep; both contain one fundamental cycle.
    expected_base=round(4096*(440*2**((48-57)/12))*(672 if sign<0 else 168)/44100)
    assert base==expected_base, (sign,base,expected_base)
    previous=0 if sign<0 else 16384
    for ms,ticks,pitch,a,b,pairs in rows:
        ms,ticks,pitch,a,b,pairs=map(int,(ms,ticks,pitch,a,b,pairs))
        assert pairs==12 and a+b==pair_gain, (sign,ms,a,b,pairs)
        t=ticks/4233600
        # The fixture records the last completed service timestamp together
        # with the registers, avoiding main-loop and diagnostic output delays.
        def expected(at):
            snap=(math.exp(-8*at/.2)-math.exp(-8))/(1-math.exp(-8)) if at<.2 else 0
            return base*2**(sign*2*snap)
        expected_pitch=expected(t)
        tolerance=expected_pitch*(2**(2/1200)-1)+1
        assert abs(pitch-expected_pitch)<=tolerance, (sign,ms,t,pitch,expected_pitch)
        assert (pitch>=previous if sign<0 else pitch<=previous), (sign,ms,pitch,previous)
        previous=pitch
        def mix(at): return max(0,min(at/.1,(.2-at)/.1,1))*pair_gain
        expected_gain=mix(t)
        assert abs(b-expected_gain)<=2, (sign,ms,t,b,expected_gain)
    assert int(rows[-1][1])>=846720 and int(rows[-1][2])==base, rows
# Each chord starts all twelve pairs in one flush and stays within the same
# deadline even while both control envelopes update every service.
chord_reports=re.findall(r'^(?:AUDIO|DISPATCH) (?:wave|sweep) chord: (.*)$',log,re.MULTILINE)
assert len(chord_reports)==14, chord_reports
for kind in ('AUDIO','DISPATCH'):
    for phase,count in (('wave',5),('sweep',2)):
        assert len(re.findall('^'+kind+' '+phase+' chord: ',log,re.MULTILINE))==count, (kind,phase)
for line in chord_reports:
    values={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)}
    if 'cost' in values:
        assert values['skipped']==values['overloads']==0 and values['interval']<=4233, values
    else:
        assert values['count']==12 and values['span']==0 and 0<values['peak']<=4233, values
sweep_stops=re.findall(r'^AUDIO sweep stop: (.*)$',log,re.MULTILINE)
assert len(sweep_stops)==2, sweep_stops
for line in sweep_stops:
    values={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)}
    assert values['vol']==values['volumes']==0, values
transient=re.search(r'TRANSIENT trials=(\d+) initial=(\d+) completed=(\d+) pending=(\d+) errors=(\d+)',log)
assert transient, 'Missing transient check'
trials,initial,completed,pending,errors=map(int,transient.groups())
assert trials==initial==completed==16 and pending>0 and errors==0, transient.groups()
print('PASS: paired wavetable/mix/sweep/headroom and emulator dispatch, loop duration, envelopes, signal, live publication, overload and pending-stop checks')

tempos=re.findall(r'TEMPO bpm=(\d+) count=(\d+) span=(\d+)',log)
assert len(tempos)==2, tempos
for bpm,count,span in tempos:
    bpm,count,span=map(int,(bpm,count,span))
    assert count>=3 and abs(span-(count-1)*4233600*60/(bpm*4))<=8467, (bpm,count,span)
reverbs=re.findall(r'REVERB size=(\d+) base=(\d+) left=(\d+) right=(\d+) send=(\d+) nonzero=(\d+)',log)
assert len(reverbs)==3, reverbs
assert len({row[1] for row in reverbs})==3, reverbs
for row in reverbs:
    size,base,left,right,send,nonzero=map(int,row)
    assert 0<base<0x80000 and left==right==16383 and send==3 and nonzero>0, row
assert re.findall(r'REVERB held size=(\d+) send=3$',log,re.MULTILINE)==['0','1','2']
assert re.findall(r'REVERB zero size=(\d+) left=0 right=0 send=3$',log,re.MULTILINE)==['0','1','2']
print('PASS: BPM clock intervals, reverb work memory, captured pair send and zero amount')

changes=re.findall(r'REVERB change size=(\d+) ticks=(\d+) cost=(\d+) interval=(\d+) playing=(\d+)',log)
assert len(changes)==3, changes
for size,ticks,cost,interval,playing in changes:
    assert int(cost)<=4233 and int(interval)<=4233 and int(playing)==1, (size,ticks,cost,interval,playing)

# Read both halves of every logical pair from the actual SPU send registers.
# Channel edits affect future starts while captured held/releasing settings
# remain unchanged. Retirement and both lifecycle paths must clear old sends.
for wet in (0,1):
    rows=re.findall(r'^CHANNEL wet='+str(wet)+r' phase=(\w+) (.*)$',log,re.MULTILINE)
    assert len(rows)==8, rows
    states={phase:{key:int(value) for key,value in re.findall(r'(\w+)=(\d+)',values)}
            for phase,values in rows}
    other=0 if wet else 12
    for phase in ('held','edited','release'):
        state=states[phase]
        assert state['send']==(3 if wet else 12) and state['count']==2, (wet,phase,state)
        assert state['a']>0 and state['b']==pair_gain and state['wave_a']!=state['wave_b'], (wet,phase,state)
    for phase in ('retired','completed'):
        assert states[phase]['send']==other and states[phase]['a']==0 and states[phase]['b']==pair_gain, (wet,phase,states[phase])
    for phase in ('reuse','restart'):
        state=states[phase]
        assert state['send']==(0 if wet else 15) and state['a']==state['b']==pair_gain, (wet,phase,state)
    assert states['reuse']['count']==3 and states['restart']['count']==2, states
    assert states['stopped']['send']==states['stopped']['a']==states['stopped']['b']==0, states
    assert all(state['left']==state['right']==16383 for state in states.values()), states
match=re.search(r'^CHANNEL tail nonzero=(\d+) send=0 left=16383$',log,re.MULTILINE)
assert match and int(match[1])>0, 'Dry reuse must preserve existing wet feedback'
print('PASS: mixed-channel SPU sends, captured hold/release, reuse in both directions, wet feedback and stop/disconnect/restart')

capacity=re.search(r'^CHANNEL capacity lanes=16 channels=8 pairs=12 send=(\d+)$',log,re.MULTILINE)
assert capacity and int(capacity[1])==0xcccccc, 'Expected twelve mixed pairs across eight channels'
for phase,count in (('channel capacity first',12),('channel capacity',24)):
    state,dispatch=fields('AUDIO '+phase),fields('DISPATCH '+phase)
    assert state['skipped']==state['steals']==state['overloads']==0, (phase,state)
    assert state['cost']<=4233 and state['interval']<=4233, (phase,state)
    assert dispatch['count']==count and 0<dispatch['peak']<=4233, (phase,dispatch)
print('PASS: eight-channel sixteen-lane first/second-lap capacity and dispatch deadline')
