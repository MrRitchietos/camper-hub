# usage: tools/shot.py out_prefix "cmd1;cmd2|name" ...  exact frames from the LVGL flush, saved as PNG (2x)
# Needs pyserial and pillow. Example: python tools/shot.py docs/screens/s "demo;tab 0|home"
import serial,time,sys,re
from PIL import Image
s=serial.Serial('/dev/ttyUSB0',115200,timeout=0.2)
def rd(t):
    o=b''; e=time.time()+t
    while time.time()<e: o+=s.read(65536)
    return o
def c(x,t=1.0): s.write((x+'\n').encode()); return rd(t)
pre=sys.argv[1]; rd(0.5)
for spec in sys.argv[2:]:
    cmds,name=spec.split('|')
    for x in cmds.split(';'):
        if x: c(x,float(__import__("os").environ.get("SHOT_WAIT","2.0")))
    rd(float(__import__("os").environ.get("SHOT_SETTLE","1.5"))); s.reset_input_buffer()
    s.write(b'shot\n')
    o=b''; e=time.time()+60
    while time.time()<e and b'\nEND' not in o[-50:]: o+=s.read(65536)
    i=o.find(b'SHOT '); j=o.find(b'\n',i); w,h=map(int,o[i+5:j].split())
    img=Image.new('RGB',(w,h)); px=img.load(); p=j+1; n=0
    while True:
        k=o.find(b'AREA ',p)
        if k<0: break
        e2=o.find(b'\n',k); x1,y1,aw,ah=map(int,o[k+5:e2].split()); d=o[e2+1:e2+1+aw*ah*2]
        if len(d)<aw*ah*2: break
        for yy in range(ah):
            for xx in range(aw):
                q=(yy*aw+xx)*2; v=d[q]|d[q+1]<<8
                px[x1+xx,y1+yy]=((v>>11&31)*255//31,(v>>5&63)*255//63,(v&31)*255//31)
        p=e2+1+aw*ah*2; n+=1
    img.resize((w*2,h*2),Image.NEAREST).save(f'{pre}-{name}.png'); print('saved',name,n,'pieces')
