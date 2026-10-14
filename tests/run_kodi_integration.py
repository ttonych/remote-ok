"""Run tests only in disposable Kodi distributions placed under this project's work/ directory."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from windows_integration import spawn, wait_for, owned_window, u

ROOT = Path(__file__).resolve().parents[1]
WORK = (ROOT / 'work').resolve()
version = (ROOT / 'VERSION').read_text().strip()
config = WORK / 'integration/kodi-coexist-desktop.ini'
config.parent.mkdir(parents=True, exist_ok=True)
config.write_text('[UI]\nLanguage=en\n[Remote]\nEnabled=0\n', encoding='ascii')
exe = ROOT / 'work/build/G60S-OK.exe'
desktop = spawn(exe, ['--config', str(config), '--tray'])
dh = wait_for(lambda: owned_window('G60S_OK_Desktop_2', desktop.pid))
try:
    assert dh, 'Isolated desktop helper did not start'
    for argument in sys.argv[1:]:
        kodi = Path(argument).resolve()
        assert kodi.is_relative_to(WORK) and (kodi / 'kodi.exe').is_file(), 'Only disposable Kodi copies under work/ are supported'
        target = kodi / 'portable_data/addons/script.g60s.ok'
        shutil.copytree(ROOT / 'addon/script.g60s.ok', target, dirs_exist_ok=True, ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
        (target / 'resources/bin/win64').mkdir(parents=True, exist_ok=True)
        shutil.copy2(exe, target / 'resources/bin/win64/G60S-OK.exe')
        harness = kodi / 'addons/service.g60s.integration'
        harness.mkdir(parents=True, exist_ok=True)
        (harness / 'addon.xml').write_text('<addon id="service.g60s.integration" name="G60S integration harness" version="1.0.0" provider-name="local test"><requires><import addon="xbmc.python" version="3.0.0"/></requires><extension point="xbmc.service" library="service.py"/></addon>', encoding='utf-8')
        shutil.copy2(ROOT / 'tests/kodi_service.py', harness / 'service.py')
        manifest = kodi / 'system/addon-manifest.xml'
        xml = ET.parse(manifest)
        if not any(item.text == 'service.g60s.integration' for item in xml.getroot()):
            ET.SubElement(xml.getroot(), 'addon').text = 'service.g60s.integration'
            xml.write(manifest, encoding='utf-8', xml_declaration=True)
        userdata = kodi / 'portable_data/userdata'
        userdata.mkdir(parents=True, exist_ok=True)
        (userdata / 'advancedsettings.xml').write_text('<advancedsettings><startwindowed>true</startwindowed></advancedsettings>')
        report = userdata / 'g60s-integration.json'
        report.unlink(missing_ok=True)
        environment = os.environ.copy(); environment['G60S_TEST_VERSION'] = version
        process = subprocess.Popen([str(kodi / 'kodi.exe'), '-p'], cwd=kodi, env=environment)
        try:
            process.wait(timeout=70)
            data = json.loads(report.read_text(encoding='utf-8'))
            assert data.get('success'), data
            assert desktop.poll() is None, 'Desktop was stopped by the Kodi service'
            assert not u.FindWindowW('G60S_OK_Kodi_' + str(process.pid), None), 'Kodi helper remained after Kodi exit'
            print(json.dumps(data, ensure_ascii=False, indent=2), flush=True)
            print('PASS desktop survives Kodi exit; Kodi helper and priority released', flush=True)
        finally:
            if process.poll() is None:
                process.terminate(); process.wait(timeout=5)
finally:
    if desktop.poll() is None:
        if dh:
            u.PostMessageW(dh, 0x111, 106, 0)
        try:
            desktop.wait(timeout=3)
        except subprocess.TimeoutExpired:
            desktop.terminate(); desktop.wait(timeout=3)
