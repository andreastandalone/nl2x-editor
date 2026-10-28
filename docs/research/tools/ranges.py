import sys
def msgs(path):
    d=open(path,'rb').read(); out=[]; i=0
    while True:
        s=d.find(b'\xf0',i)
        if s<0: break
        e=d.find(b'\xf7',s); out.append(d[s:e+1]); i=e+1
    return out
def dec(m):
    data=m[6:-1]; v=[data[2*j]|(data[2*j+1]<<4) for j in range(len(data)//2)]
    return [x-256 if x>127 else x for x in v]
mode=sys.argv[1]
if mode=='types':
    for p in sys.argv[2:]:
        ms=msgs(p); print(p.split('/')[-1], sorted(set(m[4] for m in ms)))
elif mode=='patch':
    rows=[]
    for p in sys.argv[2:]:
        for m in msgs(p):
            if len(m)==139: rows.append(dec(m))
            elif len(m)==1063:
                v=dec(m); rows += [v[k*66:(k+1)*66] for k in range(8)]
    for o in range(66):
        col=[r[o] for r in rows]; print(o, min(col), max(col), len(set(col)))
elif mode=='perf':
    rows=[dec(m) for p in sys.argv[2:] for m in msgs(p)]
    print('nvals',len(rows[0]))
    for o in range(264,len(rows[0])):
        col=[r[o] for r in rows]; print(o, min(col), max(col), sorted(set(col))[:12])
