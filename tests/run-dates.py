#!/usr/bin/env python3
"""Exercise the actual read-only FAT date parser against independent disk images."""
import pathlib,struct,tempfile,subprocess
ROOT=pathlib.Path(__file__).resolve().parents[1]
def put16(b,p,v):struct.pack_into('<H',b,p,v)
def put32(b,p,v):struct.pack_into('<I',b,p,v)
def entry(name,date=0x5d44645c,cluster=0,attr=32):
 b=bytearray(32);b[:11]=name.encode().ljust(11,b' ');b[11]=attr;put16(b,22,date&65535);put16(b,24,date>>16);put16(b,26,cluster&65535);put16(b,20,cluster>>16);return b
def lfn(name,alias,corrupt=False):
 raw=name.encode('utf-16-le');chars=list(struct.unpack('<'+'H'*(len(raw)//2),raw))+[0]
 while len(chars)%13:chars.append(65535)
 chk=0
 for x in alias.encode():chk=(((chk&1)<<7)+(chk>>1)+x)&255
 if corrupt:chk^=1
 result=[];offsets=[1,3,5,7,9,14,16,18,20,22,24,28,30]
 for ordinal in range(len(chars)//13,0,-1):
  b=bytearray(32);b[0]=ordinal|(64 if ordinal==len(chars)//13 else 0);b[11]=15;b[13]=chk
  for off,c in zip(offsets,chars[(ordinal-1)*13:ordinal*13]):put16(b,off,c)
  result.append(b)
 return result
def make(bits,path):
 clusters={12:300,16:5000,32:70000}[bits];fat_sectors=((clusters+2)*bits+4095)//4096;reserved=1;roots=0 if bits==32 else 2;start=32 if bits==16 else 0
 total=reserved+fat_sectors+roots+clusters;b=bytearray((start+total)*512)
 boot=memoryview(b)[start*512:(start+1)*512];boot[0:3]=b'\xeb\x3c\x90';put16(boot,11,512);boot[13]=1;put16(boot,14,reserved);boot[16]=1;put16(boot,17,roots*16);put16(boot,510,0xaa55)
 if total<65536:put16(boot,19,total)
 else:put32(boot,32,total)
 if bits==32:put32(boot,36,fat_sectors);put32(boot,44,2)
 else:put16(boot,22,fat_sectors)
 if start:
  b[450]=6;put32(b,454,start);put32(b,458,total);put16(b,510,0xaa55)
 fat=(start+reserved)*512;data=start+reserved+fat_sectors+roots
 def setfat(c,v):
  if bits==12:
   off=fat+c+c//2;old=int.from_bytes(b[off:off+2],'little');put16(b,off,(old&0xf if c&1 else old&0xf000)|((v&4095)<<4 if c&1 else v&4095))
  elif bits==16:put16(b,fat+c*2,v&65535)
  else:put32(b,fat+c*4,v)
 for c in range(4):setfat(c,0x0fffffff)
 root=data if bits==32 else start+reserved+fat_sectors
 rows=[entry('SHORT   NFC')]+lfn('Löng name 🦊.nfc','LONGNA~1NFC')+[entry('LONGNA~1NFC',0x5d44645d),entry('NESTED     ',cluster=3,attr=16)]+lfn('broken-long-name.nfc','BROKEN~1NFC',True)+[entry('BROKEN~1NFC'),entry('UNKNOWN NFC',0),entry('INVALID NFC',0x5d44ffff),entry('SECONDS NFC',0x5d44645f)]+lfn('x'*245+'.nfc','XXXXXX~1NFC')+[entry('XXXXXX~1NFC')]
 if bits==32:setfat(2,4);setfat(4,0x0fffffff)
 for i,row in enumerate(rows):
  loc=(root+i//16+(1 if bits==32 and i>=16 else 0))*512+(i%16)*32;b[loc:loc+32]=row
 nested=(data+1)*512;b[nested:nested+32]=entry('INSIDE  NFC',0x5d44647c)
 path.write_bytes(b)
with tempfile.TemporaryDirectory(prefix='fileman-fat-dates-') as temp:
 temp=pathlib.Path(temp);images=[temp/f'fat{n}.img' for n in (12,16,32)]
 for bits,path in zip((12,16,32),images):make(bits,path)
 exe=temp/'dates-test'
 subprocess.run(['clang','-std=c11','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(ROOT/'tests/dates.c'),str(ROOT/'src/lib/file_browser/helpers/fbp_dates.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe),*map(str,images)],check=True)
