#!/usr/bin/env python3
"""Build disposable metadata-only NFC/Tonies fixtures; never mutate the source archive."""
import argparse, os, pathlib, tempfile, json
parser=argparse.ArgumentParser()
parser.add_argument('--archive',type=pathlib.Path)
args=parser.parse_args()
root=pathlib.Path(tempfile.mkdtemp(prefix='fileman-fixtures-'))
base=root/'nfc'/'tonies';base.mkdir(parents=True)
count=0
if args.archive:
    for directory,dirs,names in os.walk(args.archive):
        relative=pathlib.Path(directory).relative_to(args.archive)
        (base/relative).mkdir(parents=True,exist_ok=True)
        for name in names:
            source=pathlib.Path(directory)/name
            if source.is_symlink():continue
            target=base/relative/name
            st=source.stat()
            target.write_bytes(b'Flipper NFC device\nVersion: 4\n')
            os.utime(target,(st.st_atime,st.st_mtime))
            count+=1
else:
    for i in range(940):
        p=base/f'category-{i%40}'/f'tag-{i}.nfc';p.parent.mkdir(parents=True,exist_ok=True);p.write_text('test')
        os.utime(p,(1700000000+i,1700000000+i));count+=1
scenarios=root/'scenarios';scenarios.mkdir()
for n in [0,1,2,3,4,5,49,50,51,99,100,101,1000]:
    folder=scenarios/f'count-{n}';folder.mkdir()
    for i in range(n):
        p=folder/f'tag-{i:05d}.nfc';p.write_bytes(b'');os.utime(p,(1700000000+i%17,1700000000+i%17))
(scenarios/'empty').mkdir()
for name in ['same.nfc','nested/same.nfc','remote.ir','nested/empty.nfc','broken.nfc','folder/file.nfc','unknown-date.nfc','UPPER.NFC','日本語.nfc','a'*245+'.nfc','noextension','.hidden.nfc','.hidden/inside.nfc']:
    p=scenarios/'actions'/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b'not a valid NFC dump\x00\xff')
# A deep tree tests scanner stack usage without reaching the explicit path limit.
p=scenarios/'deep'
for i in range(80):p=p/f'd{i}';p.mkdir(parents=True,exist_ok=True)
(p/'deep.nfc').write_bytes(b'')
# Separate stress scopes: no need to walk 10,000 files for every tiny-folder case.
large=root/'stress'/'large';large.mkdir(parents=True)
for i in range(10000):
    p=large/f'tag-{i:05d}.nfc';p.write_bytes(b'');os.utime(p,(1700000000+i,1700000000+i))
(root/'stress'/'overlong').mkdir() # Host stub injects a 1024-byte device path (macOS PATH_MAX is shorter).
print(json.dumps({'fixture':str(root),'archive_entries':count,'source_unchanged':True}))
