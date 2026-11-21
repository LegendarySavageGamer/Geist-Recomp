"""Local analysis only: finds instruction references in the owner's disc program."""
import sys,struct
sys.path.insert(0,'gcn-recomp/recomp')
from dol import Dol,load_symbols
d=Dol(sys.argv[1]);symbols=load_symbols('analysis/symbols.txt')
for needle in (b'Camera Pitch =',b'Look =',b'DeltaAlpha.yaw'):
    pos=0
    while (pos:=d.data.find(needle,pos))>=0:
        va=next(a+pos-o for a,s,o,t in d.sections if o<=pos<o+s)
        print(needle.decode(),hex(va))
        for a,s,o in d.text_sections():
            regs={}
            for p in range(o,o+s,4):
                ins=struct.unpack_from('>I',d.data,p)[0];op=ins>>26;rt=(ins>>21)&31;ra=(ins>>16)&31;rb=(ins>>11)&31
                imm=(ins&65535);signed=imm if imm<32768 else imm-65536;value=None;dest=rt
                if op==15:value=((regs.get(ra) if ra else 0)+ (signed<<16))&0xffffffff if not ra or ra in regs else None
                elif op==14:value=((regs.get(ra) if ra else 0)+signed)&0xffffffff if not ra or ra in regs else None
                elif op in (24,25):dest=ra;value=regs[rt]|(imm<<(16 if op==25 else 0)) if rt in regs else None
                elif op==31 and ((ins>>1)&1023)==444:dest=ra;value=regs.get(rt) if rt==rb else None
                elif op in (18,19):
                    if ins&1:
                        for r in range(13):regs.pop(r,None)
                    continue
                elif op in (16,10,11,32+4,37,38,39,44,45,47,52,53,54,55):continue
                if value is None:regs.pop(dest,None)
                else:regs[dest]=value
                if value==va:
                    pc=a+p-o;fn=next((v['name'] for v in symbols if v['type']=='function' and v['addr']<=pc<v['addr']+(v['size'] or 0)), '?')
                    print('flow',hex(pc),fn)
        hi=((va+0x8000)>>16)&65535;lo=va&65535
        for a,s,o in d.text_sections():
            for p in range(o,o+s,4):
                ins=struct.unpack_from('>I',d.data,p)[0]
                if ins>>26!=15 or (ins>>16)&31 or ins&65535!=hi:continue
                reg=(ins>>21)&31
                for q in range(p+4,min(p+164,o+s),4):
                    x=struct.unpack_from('>I',d.data,q)[0]
                    if x>>26==14 and (x>>16)&31==reg and x&65535==lo:
                        pc=a+q-o;fn=next((v['name'] for v in symbols if v['type']=='function' and v['addr']<=pc<v['addr']+(v['size'] or 0)), '?')
                        print(hex(pc),fn)
        pos+=len(needle)
