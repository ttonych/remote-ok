"""Package the two deliveries from an explicit allowlist. Local builds are visibly marked."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import struct
import subprocess
import zipfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
VERSION = (ROOT / 'VERSION').read_text(encoding='utf-8').strip()
ADDON = ROOT / 'addon/script.g60s.ok'
ADDON_FILES = ['addon.xml', 'default.py', 'service.py', 'resources/__init__.py',
               'resources/bridge.py', 'resources/settings.xml',
               'resources/language/resource.language.en_gb/strings.po',
               'resources/language/resource.language.ru_ru/strings.po']
DOC_FILES = ['README.md', 'README.en.md', 'LICENSE', 'roadmap.md', 'AGENTS.md', 'docs/testing.md', 'docs/ui-review.md',
             'docs/build.md', 'docs/architecture.md', 'docs/credits.md',
             'docs/images/desktop-en.png', 'docs/images/desktop-ru.png',
             'docs/images/discovery-ru.png', 'docs/images/diagnostics-ru.png',
             'docs/images/kodi-en.png', 'docs/images/kodi-ru.png',
             'docs/images/dpi-144-top.png', 'docs/images/dpi-192-bottom.png']

def sha(data):
    return hashlib.sha256(data).hexdigest()

def commit():
    result = subprocess.run(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else None

def validate_exe(data):
    assert data[:2] == b'MZ', 'Missing Windows executable'
    offset = struct.unpack_from('<I', data, 0x3C)[0]
    assert data[offset:offset + 4] == b'PE\0\0', 'Missing PE header'
    assert struct.unpack_from('<H', data, offset + 4)[0] == 0x8664, 'Expected Windows x64'

def write_zip(path, entries):
    temporary = path.with_suffix('.zip.tmp')
    with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(entries.items()):
            assert '\\' not in name and '..' not in PurePosixPath(name).parts and not name.startswith('/')
            assert not name.endswith(('.ini', '.pyc', '.log')) and '__pycache__' not in name
            info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data)
    with zipfile.ZipFile(temporary) as archive:
        assert archive.testzip() is None, 'Archive CRC failure'
        assert set(archive.namelist()) == set(entries), 'Unexpected archive members'
    temporary.replace(path)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', required=True, type=Path)
    parser.add_argument('--release', action='store_true')
    args = parser.parse_args()
    revision = commit()
    if args.release:
        assert os.environ.get('GITHUB_ACTIONS') == 'true', 'Public binaries must come from GitHub Actions'
        assert os.environ.get('GITHUB_REF') == 'refs/tags/v' + VERSION, 'Tag must match VERSION'
        assert revision and revision == os.environ.get('GITHUB_SHA'), 'Checkout must match the tagged commit'
        result = subprocess.run(['git', '-C', str(ROOT), 'status', '--porcelain', '--untracked-files=no'], capture_output=True, text=True, check=True)
        assert not result.stdout.strip(), 'Tracked source changed during the release build'
    binary = args.exe.read_bytes(); validate_exe(binary)
    assert ET.parse(ADDON / 'addon.xml').getroot().attrib['version'] == VERSION
    suffix = '' if args.release else '-local'
    destination = ROOT / ('dist' if args.release else 'outputs') / ('v' + VERSION + suffix)
    destination.mkdir(parents=True, exist_ok=True)
    metadata = json.dumps({'version': VERSION, 'kind': 'github-actions-release' if args.release else 'local-test-build',
                           'commit': revision, 'exe_sha256': sha(binary),
                           'built_at_utc': datetime.now(timezone.utc).isoformat(),
                           'github_run_id': os.environ.get('GITHUB_RUN_ID') if args.release else None},
                          ensure_ascii=False, indent=2).encode('utf-8')
    docs = {name: (ROOT / name).read_bytes() for name in DOC_FILES}
    desktop = {'G60S-OK/' + name: data for name, data in docs.items()}
    desktop.update({'G60S-OK/G60S-OK.exe': binary, 'G60S-OK/build-info.json': metadata})
    addon = {'script.g60s.ok/' + name: (ADDON / name).read_bytes() for name in ADDON_FILES}
    addon.update({'script.g60s.ok/' + name: data for name, data in docs.items()})
    addon.update({'script.g60s.ok/resources/bin/win64/G60S-OK.exe': binary, 'script.g60s.ok/build-info.json': metadata})
    packages = [(destination / ('G60S-OK-Windows-x64-' + VERSION + suffix + '.zip'), desktop),
                (destination / ('script.g60s.ok-' + VERSION + suffix + '.zip'), addon)]
    sums = []
    for path, entries in packages:
        write_zip(path, entries)
        sums.append(sha(path.read_bytes()) + '  ' + path.name)
        print('PASS packaged and verified ' + path.name + ' (' + str(path.stat().st_size) + ' bytes)')
    (destination / 'SHA256SUMS.txt').write_text('\n'.join(sums) + '\n', encoding='ascii')
    print('PASS both packages contain the identical Windows x64 helper; personal profiles and logs excluded')
    print(destination)

if __name__ == '__main__':
    main()
