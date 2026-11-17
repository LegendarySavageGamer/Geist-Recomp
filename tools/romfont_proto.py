# Prototype: OSFont (expanded, I4) -> IPL ROM font image (2bpp sheet, Yay0-compressed)
import struct, sys
def yay0_compress(src):
    # Greedy LZ in Yay0 form: mask bits (1=literal byte), links (16-bit), chunk bytes.
    masks=[]; links=bytearray(); chunks=bytearray()
    bits=[]; i=0; n=len(src)
    # hash chains for speed
    from collections import defaultdict
    table=defaultdict(list)
    while i<n:
        best_len=0; best_dist=0
        if i+3<=n:
            key=src[i:i+3]
            cands=table.get(key,[])
            for j in reversed(cands[-64:]):
                d=i-j
                if d>0x1000: break
                l=3
                mx=min(0x111,n-i)
                while l<mx and src[j+l]==src[i+l]: l+=1
                if l>best_len: best_len,best_dist=l,d
                if l==mx: break
        if best_len>=3:
            bits.append(0)
            d=best_dist-1
            if best_len<=0x11:
                links+=struct.pack('>H',((best_len-2)<<12)|d)
            else:
                links+=struct.pack('>H',d); chunks.append(best_len-0x12)
            for k in range(best_len):
                if i+k+3<=n: table[src[i+k:i+k+3]].append(i+k)
            i+=best_len
        else:
            bits.append(1); chunks.append(src[i])
            if i+3<=n: table[src[i:i+3]].append(i)
            i+=1
    while len(bits)%32: bits.append(0)
    maskb=bytearray()
    for w in range(0,len(bits),32):
        v=0
        for b in bits[w:w+32]: v=(v<<1)|b
        maskb+=struct.pack('>I',v)
    link_off=16+len(maskb); chunk_off=link_off+len(links)
    return b'Yay0'+struct.pack('>III',n,link_off,chunk_off)+maskb+links+chunks
def yay0_decode(s):
    size,lo,co=struct.unpack('>III',s[4:16]); out=bytearray(); mp=16; mask=0; cnt=0
    while len(out)<size:
        if cnt==0: mask=struct.unpack('>I',s[mp:mp+4])[0]; mp+=4; cnt=32
        if mask&0x80000000: out.append(s[co]); co+=1
        else:
            v=struct.unpack('>H',s[lo:lo+2])[0]; lo+=2
            d=(v&0xFFF)+1; l=v>>12
            if l==0: l=s[co]+0x12; co+=1
            else: l+=2
            for k in range(l): out.append(out[-d])
        mask<<=1; cnt-=1
    return bytes(out)
d=open(sys.argv[1],'rb').read()
hdr=bytearray(d[:0x30]); wt=d[0x30:0x110]; img=d[0x110:0x110+0x20000]
q=lambda nib: (nib*3+7)//15   # nearest of 0,5,10,15 -> 0..3
packed=bytearray(len(img)//2)
for i in range(len(packed)):
    a,b=img[2*i],img[2*i+1]
    packed[i]=(q(a>>4)<<6)|(q(a&15)<<4)|(q(b>>4)<<2)|q(b&15)
rom=bytes(hdr)+wt+bytes(packed)
c=yay0_compress(rom)
print('uncompressed',hex(len(rom)),'compressed',hex(len(c)),'fits 0x3000:',len(c)<=0x3000)
assert yay0_decode(c)==rom
print('round-trip ok')
open('/tmp/romfont.yay0','wb').write(c)
