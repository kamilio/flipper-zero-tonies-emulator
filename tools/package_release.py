#!/usr/bin/env python3
"""Build a source-complete release from an explicit public-file allowlist."""
import argparse
import hashlib
from pathlib import Path
import re
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PUBLIC_FILES = (
    '.gitignore', '.gitattributes', 'README.md', 'LICENSE', 'NOTICE', 'CHANGELOG.md',
    'CONTRIBUTING.md', 'application.fam', 'tonie_10px.png', 'Makefile',
    'tests/test_quiet_recovery.c', 'tests/test_log_sink.c',
)
PUBLIC_TREES = ('.github', 'src', 'tools', 'docs', 'firmware')
FORBIDDEN_SUFFIXES = {'.nfc', '.log', '.pem', '.key', '.gcda', '.gcno', '.pyc', '.mp3'}

def source_files():
    paths = [ROOT / name for name in PUBLIC_FILES]
    for name in PUBLIC_TREES:
        paths.extend(p for p in (ROOT / name).rglob('*') if p.is_file())
    for path in sorted(paths):
        relative = path.relative_to(ROOT)
        if path.is_symlink() or '__pycache__' in relative.parts or path.name == '.DS_Store' or str(relative) == 'tools/trace_replay.c':
            continue
        if path.suffix in FORBIDDEN_SUFFIXES or path.name.startswith('.env'):
            raise ValueError(f'Private/generated file in public tree: {relative}')
        yield path, str(relative)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fap', required=True, type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'dist' / 'release')
    parser.add_argument('--tag', help='Require tag to match the manifest version')
    args = parser.parse_args()
    version = re.search(r'fap_version="([0-9]+\.[0-9]+\.[0-9]+)"',
                        (ROOT / 'application.fam').read_text()).group(1)
    if args.tag and args.tag != f'v{version}':
        parser.error(f'tag {args.tag} does not match version {version}')
    if not args.fap.is_file():
        parser.error('FAP does not exist')
    args.output.mkdir(parents=True, exist_ok=True)
    binary = args.output / 'tonie_emulator.fap'
    if args.fap.resolve() != binary.resolve():
        shutil.copyfile(args.fap, binary)
    source = args.output / f'tonie-emulator-{version}-source.zip'
    with zipfile.ZipFile(source, 'w', zipfile.ZIP_DEFLATED) as archive:
        for path, name in source_files():
            info = zipfile.ZipInfo(f'tonie-emulator-{version}/{name}', (2020, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, path.read_bytes())
    (args.output / 'SHA256SUMS').write_text(''.join(
        f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n'
        for p in (binary, source)))
    print(f'Release {version}: {binary.name}, {source.name}, SHA256SUMS')

if __name__ == '__main__':
    main()
