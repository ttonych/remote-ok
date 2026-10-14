"""Fast source validation, also run by the release build. No Kodi import needed."""
import ast
from pathlib import Path
import re
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
addon = root / 'addon/script.g60s.ok'
version = (root / 'VERSION').read_text().strip()
assert re.fullmatch(r'\d+\.\d+\.\d+', version)
for source in addon.rglob('*.py'):
    ast.parse(source.read_text(encoding='utf-8'), filename=str(source), feature_version=(3, 8))
for source in addon.rglob('*.xml'):
    ET.parse(source)
metadata = ET.parse(addon / 'addon.xml').getroot()
assert metadata.attrib['version'] == version, 'Add-on and application versions differ'
assert metadata.attrib['id'] == 'script.g60s.ok'
assert {e.attrib['point'] for e in metadata.findall('extension')} >= {'xbmc.python.script', 'xbmc.service'}
locale_ids = []
locale_messages = []
for language in ('en_gb', 'ru_ru'):
    text = (addon / ('resources/language/resource.language.' + language) / 'strings.po').read_text(encoding='utf-8')
    ids = re.findall(r'msgctxt "#(\d+)"', text)
    assert len(ids) == len(set(ids)), 'Duplicate localization IDs'
    locale_ids.append(set(ids))
    locale_messages.append(dict(re.findall(r'msgctxt "#(\d+)"\s+msgid "(.*)"', text)))
    assert 'G60S OK ' + version in text, 'Locale header version differs'
assert locale_ids[0] == locale_ids[1]
assert locale_messages[0] == locale_messages[1], 'Translated locale has stale source strings'
used = set()
for source in addon.glob('*.py'):
    used |= set(re.findall(r'getLocalizedString\((\d+)\)', source.read_text(encoding='utf-8')))
assert used <= locale_ids[0], 'Missing notification translations'
ET.parse(root / 'src/app.manifest')
print('PASS Python 3.8 syntax, XML, version agreement and Kodi localization coverage')
