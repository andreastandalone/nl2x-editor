import sys, collections
for path in sys.argv[1:]:
    d = open(path,'rb').read()
    msgs=[]; i=0
    while True:
        s=d.find(b'\xf0',i)
        if s<0: break
        e=d.find(b'\xf7',s); msgs.append(d[s:e+1]); i=e+1
    print('==',path.split('/')[-1],len(d),'bytes',len(msgs),'msgs')
    c=collections.Counter((m[1],m[2],m[3],m[4],len(m)) for m in msgs)
    for k,v in sorted(c.items()): print('  hdr mfr=%02X dev=%02X model=%02X type=%d len=%d  x%d'%(k+(v,)))
    specs=[m[5] for m in msgs]; print('  specs', specs[:5],'...',specs[-12:])
    m=msgs[0]; data=m[6:-1]
    vals=[ (data[2*j] | (data[2*j+1]<<4)) for j in range(len(data)//2)]
    vals=[v-256 if v>127 else v for v in vals]
    print('  first msg hex head:', m[:8].hex(' '), ' decoded first 66:', vals[:66])
    if len(vals)>264: print('  perf tail:', vals[264:])
    print('  max nibble', max(data))
