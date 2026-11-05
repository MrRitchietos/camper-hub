# Touch handling test over serial (board on /dev/ttyUSB0, Tools > Settings scrolled from the top).
# Taps the Alarm sound value twice (on, then back), so it ends as it started. Needs pyserial.
import serial,time,sys
s=serial.Serial('/dev/ttyUSB0',115200,timeout=0.2)
time.sleep(18); s.read(100000)   # opening the port restarts the board
def c(x,t=60):
    s.write((x+'\n').encode()); o=b''; e=time.time()+t
    while time.time()<e:
        o+=s.read(4096)
        if b'done' in o: break
    time.sleep(0.5); o+=s.read(4096); t=o.decode(errors='replace')
    if 'Rebooting' in t or 'wdt' in t: return 'CRASH'
    return [l for l in t.splitlines() if l.startswith('list')]
def raw(x,w=2): s.write((x+'\n').encode()); time.sleep(w); s.read(100000)
raw('close'); raw('tab 5'); raw('ts 2')
ok=True
def chk(name,got,want):
    global ok; r='OK' if got==want else 'FAIL'; ok&= got==want; print(f'{r:4} {name}: {got}')
chk('slow scroll 150px', c('swipe 160 190 160 40 2000'), [])
chk('tap LEFT side of Alarm sound', c('tap 60 165'), [])
chk('tap value of Alarm sound', c('tap 280 165'), ['list tap 8'])
chk('tap it again (back)', c('tap 280 165'), ['list tap 8'])
chk('finger slides 15px on value', c('swipe 280 165 280 180 150'), [])
chk('finger slides 15px sideways', c('swipe 280 165 265 165 150'), [])
s.write(b'swipe 160 190 160 110 60\n'); o=b''; e=time.time()+10
while b'done' not in o and time.time()<e: o+=s.read(4096)
chk('tap right after a flick (stops glide)', c('tap 280 120'), [])
for i in range(7):
    raw('swipe 160 60 160 200 1500',3)       # slowly back to the top, list rests
    time.sleep(1)
    s.write(b'swipe 160 190 160 90 50\n'); o=b''; e=time.time()+10
    while b'done' not in o and time.time()<e: o+=s.read(4096)
    time.sleep(0.05*i)
    chk(f'flick up + tap {50*i}ms later', c('tap 280 100'), [])
raw('swipe 160 60 160 200 1500',3); time.sleep(1)
chk('list at rest, tap left side of WiFi row', c('tap 60 95'), [])
print('ALL OK' if ok else 'SOME FAILED')
