# Hex Isle (Plasma 3.0) init/*.ini codec: AES-128-ECB, key recovered from plasma.exe @0x9a1d8c (xor 0xCF092676)
# usage: python hexini.py d|e <in> <out>
import struct,sys
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
raw=bytes.fromhex('866b2cfcda7b2e95e83e71f1130a41c7fe648ec2')
K=0xcf092676
w=[struct.unpack_from('<I',raw,4*i)[0]^K for i in range(5)]
KEY=struct.pack('<4I',*w[:4]); MAGIC=w[4]
def dec(b):
    m,n=struct.unpack_from('<II',b); assert m==MAGIC, hex(m)
    d=Cipher(algorithms.AES(KEY),modes.ECB()).decryptor()
    return (d.update(b[8:])+d.finalize())[:n]
def enc(p):
    pad=p+b'\0'*(-len(p)%16)
    e=Cipher(algorithms.AES(KEY),modes.ECB()).encryptor()
    return struct.pack('<II',MAGIC,len(p))+e.update(pad)+e.finalize()
if __name__=='__main__':
    op,src,dst=sys.argv[1:4]
    data=open(src,'rb').read()
    open(dst,'wb').write(dec(data) if op=='d' else enc(data))
