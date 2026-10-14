"""Simulated 144/192-DPI layout checks; does not change Windows display settings."""
import ctypes as c
from ctypes import wintypes as w
import json
import os
from pathlib import Path
import subprocess
import time
from windows_integration import ROOT, WORK, u, owned_window, wait_for, capture, choose, click, check, results

u.GetClientRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
u.GetScrollPos.argtypes = [w.HWND, c.c_int]; u.GetScrollPos.restype = c.c_int
for dpi, language in ((144, 'en'), (192, 'ru')):
    config = WORK / ('dpi-' + str(dpi) + '.ini')
    config.write_text('[UI]\nLanguage=' + language + '\n[Remote]\nEnabled=0\n', encoding='ascii')
    env = os.environ.copy(); env['G60S_TEST_DPI'] = str(dpi)
    process = subprocess.Popen([str(ROOT / 'work/build/dispatch-probe.exe'), '--config', str(config)],
                               env=env, creationflags=subprocess.CREATE_NO_WINDOW)
    h = None
    try:
        h = wait_for(lambda: owned_window('G60S_OK_Desktop_2', process.pid))
        assert h
        time.sleep(.5)
        capture(h, process.pid, '08-dpi-' + str(dpi) + '-top.png')
        client = w.RECT(); u.GetClientRect(h, c.byref(client))
        check(client.right >= 736 * dpi // 96, str(dpi) + ' simulated DPI: all controls fit horizontally')
        click(h, 113)
        u.SendMessageW(h, 0x115, 7, 0)  # WM_VSCROLL / SB_BOTTOM
        capture(h, process.pid, '09-dpi-' + str(dpi) + '-bottom.png')
        check(u.GetScrollPos(h, 1) > 0, str(dpi) + ' simulated DPI: expanded content can scroll')
        bounds = w.RECT(); u.GetWindowRect(u.GetDlgItem(h, 109), c.byref(bounds))
        outer = w.RECT(); u.GetWindowRect(h, c.byref(outer))
        check(bounds.bottom <= outer.bottom, str(dpi) + ' simulated DPI: diagnostics copy button remains reachable')
    finally:
        if h: click(h, 106)
        try: process.wait(timeout=3)
        except subprocess.TimeoutExpired: process.terminate(); process.wait(timeout=3)
(WORK / 'dpi-results.json').write_text(json.dumps(results, indent=2))
