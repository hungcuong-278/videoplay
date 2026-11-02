import numpy as np, sys, collections
P="""212222 222122 222221 121223 121322 131222 122213 122312 132212 221213 221312 231212 112232 122132 122231 113222 123122 123221 223211 221132 221231 213212 223112 312131 311222 321122 321221 312212 322112 322211 212123 212321 232121 111323 131123 131321 112313 132113 132311 211313 231113 231311 112133 112331 132131 113123 113321 133121 313121 211331 231131 213113 213311 213131 311123 311321 331121 312113 312311 332111 314111 221411 431111 111224 111422 121124 121421 141122 141221 112214 112412 122114 122411 142112 142211 241211 221114 413111 241112 134111 111242 121142 121241 114212 124112 124211 411212 421112 421211 212141 214121 412121 111143 111341 131141 114113 114311 411113 411311 113141 114131 311141 411131 211412 211214 211232""".split()
TAB={p:i for i,p in enumerate(P)}
raw=np.fromfile(sys.argv[1],np.uint8).reshape(-1,8,160).astype(float)
prof=raw[:,0:6,:].mean(axis=1)
def edges(r,th):
    e=[]
    for x in range(len(r)-1):
        if (r[x]-th)*(r[x+1]-th)<0: e.append(x+(th-r[x])/(r[x+1]-r[x]))
    return np.array(e)
def parse(vals):
    # vals: symbol values after start; last is checksum
    if len(vals)<2: return None
    start=vals[0]; data=vals[1:-1]; chk=vals[-1]
    s=start+sum((i+1)*v for i,v in enumerate(data))
    if s%103!=chk: return None
    mode={103:'A',104:'B',105:'C'}.get(start); txt=''
    for v in data:
        if mode=='C':
            if v<100: txt+=f'{v:02d}'
            elif v==100: mode='B'
            elif v==101: mode='A'
            else: return None
        else:
            if v==99: mode='C'
            elif v<95: txt+=chr(32+v)
            else: return None
    return txt
def decode(f):
    for th in (128,110,145,95,160):
        e=edges(prof[f,0:140],th); w=np.diff(e)
        mods=list(np.maximum(1,np.round(w/(4/3))).astype(int))
        s='2'+''.join(map(str,mods))       # first start bar is clipped at image edge
        vals=[];i=0;ok=False
        while i+6<=len(s):
            if s[i:i+6]=='233111': ok=True;break
            v=TAB.get(s[i:i+6])
            if v is None: break
            vals.append(v); i+=6
        if ok:
            t=parse(vals)
            if t is not None: return t
    return None
ids=[decode(f) for f in range(len(prof))]
np.save(sys.argv[2], np.array([int(t) if t else -1 for t in ids]))
bad=[f for f,t in enumerate(ids) if t is None]
print('frames',len(ids),'decoded',len(ids)-len(bad),'failed',len(bad), bad[:20])
