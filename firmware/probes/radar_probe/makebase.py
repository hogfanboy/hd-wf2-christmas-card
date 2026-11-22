# Basemap for the radar probe (radar_probe.ino): OSM tiles around home -> 8192-byte class map for POST /base.
# Usage: py makebase.py <lat> <lon> <out.bin>   then: curl -F "base=@out.bin" http://<card>/base
# Classes per LED, row by row: 0 nothing, 1 water, 2 shore (water next to land), 3 interstate, 4 state line.
# Window = 512x256 OSM z8 px centered on home (= the radar's 256x128 z7 px), 4x4 OSM px per LED. Classes come
# from OSM Carto colors (water #aad3df, motorway pink, state line = purple blended into land).
# Tiles: tile.openstreetmap.org (light use, cached in ./cache). Map data (c) OpenStreetMap contributors.
import sys,struct,zlib,urllib.request,os,math
def readpng_bytes(d):
    i=8; idat=b''; pal=None; trns=None
    while i<len(d):
        n,=struct.unpack('>I',d[i:i+4]); t=d[i+4:i+8]; c=d[i+8:i+8+n]
        if t==b'IHDR': w,h,bd,ct=struct.unpack('>IIBB',c[:10])
        elif t==b'PLTE': pal=[tuple(c[k:k+3]) for k in range(0,n,3)]
        elif t==b'tRNS': trns=list(c)
        elif t==b'IDAT': idat+=c
        i+=12+n
    raw=zlib.decompress(idat)
    chans={6:4,2:3,3:1,0:1,4:2}[ct]; bits=chans*bd; bpp=max(1,bits//8); stride=(w*bits+7)//8
    rows=[]; prev=bytearray(stride); p=0
    for y in range(h):
        f=raw[p]; line=bytearray(raw[p+1:p+1+stride]); p+=1+stride
        for x in range(stride):
            a=line[x-bpp] if x>=bpp else 0; b=prev[x]; c=prev[x-bpp] if x>=bpp else 0
            if f==1: line[x]=(line[x]+a)&255
            elif f==2: line[x]=(line[x]+b)&255
            elif f==3: line[x]=(line[x]+((a+b)>>1))&255
            elif f==4:
                pa=abs(b-c); pb=abs(a-c); pc=abs(a+b-2*c)
                line[x]=(line[x]+(a if pa<=pb and pa<=pc else (b if pb<=pc else c)))&255
        prev=line
        px=[]
        for x in range(w):
            if ct==6: px.append(tuple(line[x*4:x*4+4]))
            elif ct==2: px.append(tuple(line[x*3:x*3+3])+(255,))
            elif ct==3:
                if bd==8: k=line[x]
                elif bd==4: k=(line[x>>1]>>((1-(x&1))*4))&15
                else: k=(line[x>>3]>>(7-(x&7)))&1 if bd==1 else (line[x>>2]>>((3-(x&3))*2))&3
                a=trns[k] if trns and k<len(trns) else 255
                px.append(pal[k]+(a,))
            else: px.append((line[x],)*3+(255,))
        rows.append(px)
    return w,h,rows
def writepng(fn,w,h,rows):
    raw=b''.join(b'\x00'+bytes(r) for r in rows)
    def ch(t,d): return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
    open(fn,'wb').write(b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+ch(b'IDAT',zlib.compress(raw))+ch(b'IEND',b''))
def get(url,cache):
    fn=os.path.join(cache,url.split('://')[1].replace('/','_'))
    if not os.path.exists(fn):
        req=urllib.request.Request(url,headers={'User-Agent':'PocketJumbotron-radar-probe/0.1 (personal test)'})
        open(fn,'wb').write(urllib.request.urlopen(req,timeout=20).read())
    return open(fn,'rb').read()
def gpx(lat,lon,z):
    n=256*2**z; return (lon+180)/360*n, (1-math.asinh(math.tan(math.radians(lat)))/math.pi)/2*n
def window(lat,lon,z,ww,hh,urlf,cache):
    cx,cy=gpx(lat,lon,z); x0=int(round(cx))-ww//2; y0=int(round(cy))-hh//2
    out=[[None]*ww for _ in range(hh)]; tiles={}
    for ty in range(y0//256,(y0+hh-1)//256+1):
        for tx in range(x0//256,(x0+ww-1)//256+1):
            w,h,rows=readpng_bytes(get(urlf(z,tx,ty),cache)); tiles[(tx,ty)]=rows
    for y in range(hh):
        gy=y0+y
        for x in range(ww):
            gx=x0+x; out[y][x]=tiles[(gx//256,gy//256)][gy%256][gx%256]
    return out
W,H=128,64
lat,lon,out=float(sys.argv[1]),float(sys.argv[2]),sys.argv[3]
def classify(p):
    r,g,b,a=p
    if abs(r-170)<14 and abs(g-211)<14 and abs(b-223)<14: return 1
    if r>200 and 110<g<175 and 120<b<185 and r-g>45: return 3
    if r<215 and r-g>=12 and b-g>=6 and r>=b: return 4
    return 0
os.makedirs('cache',exist_ok=True)
win=window(lat,lon,8,W*4,H*4,lambda z,x,y:f'https://tile.openstreetmap.org/{z}/{x}/{y}.png','cache')
cls=[[0]*W for _ in range(H)]
for Y in range(H):
    for X in range(W):
        c=[0]*5
        for dy in range(4):
            for dx in range(4): c[classify(win[Y*4+dy][X*4+dx])]+=1
        cls[Y][X]=3 if c[3]>=2 else 4 if c[4]>=2 else 1 if c[1]>=8 else 0
outc=[r[:] for r in cls]
for Y in range(H):
    for X in range(W):
        if cls[Y][X]==1 and any(0<=Y+dy<H and 0<=X+dx<W and cls[Y+dy][X+dx]!=1 for dy,dx in ((1,0),(-1,0),(0,1),(0,-1))): outc[Y][X]=2
open(out,'wb').write(bytes(v for r in outc for v in r))
print(out, sum(r.count(1) for r in outc),'water',sum(r.count(2) for r in outc),'shore',sum(r.count(3) for r in outc),'road',sum(r.count(4) for r in outc),'state')
