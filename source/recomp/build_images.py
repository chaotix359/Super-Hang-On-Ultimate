import os,sys
R=sys.argv[1]; O=sys.argv[2]; os.makedirs(O, exist_ok=True)
rd=lambda n: open(os.path.join(R,n),'rb').read()
def il(even,odd):
    a,b=rd(even),rd(odd); out=bytearray(len(a)*2)
    out[0::2]=a; out[1::2]=b; return bytes(out)
main=il("epr-10886.133","epr-10884.118")+il("epr-10887.132","epr-10885.117")
sub =il("epr-10792.76","epr-10790.58")+il("epr-10793.75","epr-10791.57")
open(f"{O}/main.bin","wb").write(main); open(f"{O}/sub.bin","wb").write(sub)
open(f"{O}/tiles.bin","wb").write(rd("epr-10652.54")+rd("epr-10651.55")+rd("epr-10650.56"))
spr=bytearray(0x100000)
for base,odd,even in [(0,"mpr-10794.8","mpr-10798.16"),(0x40000,"mpr-10795.6","mpr-10799.14"),(0x80000,"mpr-10796.4","mpr-10800.12"),(0xc0000,"mpr-10797.2","mpr-10801.10")]:
    spr[base:base+0x40000]=il(even,odd)
open(f"{O}/sprites.bin","wb").write(spr)
open(f"{O}/road.bin","wb").write(rd("epr-10642.47"))
open(f"{O}/sound.bin","wb").write(rd("epr-10649c.88"))
pcm=b"".join(rd(n)*2 for n in ["epr-10643.66","epr-10644.67","epr-10645.68","epr-10646.69"])
open(f"{O}/pcm.bin","wb").write(pcm)
