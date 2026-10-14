"""Local Win32 lifecycle and UI checks. Own test processes and profiles only."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / 'work/integration'
WORK.mkdir(parents=True, exist_ok=True)
spec = importlib.util.spec_from_file_location('g60s_bridge', ROOT / 'addon/script.g60s.ok/resources/bridge.py')
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)
u, k = bridge.user, bridge.kernel
u.GetDlgItem.argtypes = [w.HWND, c.c_int]; u.GetDlgItem.restype = w.HWND
u.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]; u.SendMessageW.restype = w.LPARAM
u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.IsWindowVisible.argtypes = [w.HWND]; u.IsWindowVisible.restype = w.BOOL
u.IsWindowEnabled.argtypes = [w.HWND]; u.IsWindowEnabled.restype = w.BOOL
u.ShowWindow.argtypes = [w.HWND, c.c_int]; u.ShowWindow.restype = w.BOOL
u.GetForegroundWindow.restype = w.HWND
u.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
u.SetWindowPos.argtypes = [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT]
results = []

def check(condition, message):
    if not condition:
        raise AssertionError(message)
    results.append('PASS ' + message)
    print(results[-1], flush=True)

def wait_for(predicate, seconds=5):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        result = predicate()
        if result:
            return result
        time.sleep(.05)
    return None

def owned_window(class_name, pid):
    h = u.FindWindowW(class_name, None)
    actual = w.DWORD()
    if h:
        u.GetWindowThreadProcessId(h, c.byref(actual))
    status = u.GetDlgItem(h, 107) if h else None
    return h if actual.value == pid and status and u.SendMessageW(status, 0xE, 0, 0) else None

def text(h, control):
    buffer = c.create_unicode_buffer(4096)
    u.SendMessageW(u.GetDlgItem(h, control), 0xD, len(buffer), c.addressof(buffer))
    return buffer.value

def click(h, control):
    u.SendMessageW(u.GetDlgItem(h, control), 0xF5, 0, 0)

def choose(h, control, index):
    child = u.GetDlgItem(h, control)
    u.SendMessageW(child, 0x14E, index, 0)
    u.SendMessageW(h, 0x111, control | (1 << 16), child)

def capture(h, pid, name):
    helper = Path(os.environ.get('G60S_CAPTURE_HELPER', str(Path.home() / '.codex/skills/windows-live-capture/scripts/capture_window.py')))
    if not helper.exists():
        return
    u.ShowWindow(h, 9); u.SetForegroundWindow(h)
    u.SetWindowPos(h, None, 30, 30, 0, 0, 0x15)  # preserve size/z-order
    time.sleep(.2)
    subprocess.run([sys.executable, str(helper), '--hwnd', str(h), '--pid', str(pid),
                    '--output', str(WORK / name)], check=True, stdout=subprocess.DEVNULL)

def spawn(exe, arguments):
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    return subprocess.Popen([str(exe)] + arguments, startupinfo=startup,
                            creationflags=subprocess.CREATE_NO_WINDOW,
                            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def main(executable=None):
    exe = Path(executable).resolve() if executable else ROOT / 'work/build/G60S-OK.exe'
    profile = WORK / 'profile with spaces'
    profile.mkdir(exist_ok=True)
    config = profile / 'desktop.ini'
    config.write_text('[UI]\nLanguage=en\n[Remote]\nPath=NOT_CONNECTED\nEnabled=1\n', encoding='ascii')
    desktop = spawn(exe, ['--config', str(config)])
    kodi = None
    h = None
    try:
        h = wait_for(lambda: owned_window('G60S_OK_Desktop_2', desktop.pid))
        check(h and desktop.poll() is None, 'desktop starts with an isolated profile path containing spaces')
        check(not u.IsWindowEnabled(u.GetDlgItem(h, 104)), 'unavailable saved device cannot enable input')
        check('Waiting for the saved remote' in text(h, 107), 'unavailable saved device has a clear waiting status')
        choose(h, 101, 0)  # clear intentionally stale selection
        capture(h, desktop.pid, '02-desktop-en.png')
        choose(h, 111, 1); choose(h, 112, 1)
        check('Mode=mpc' in config.read_text() and 'MpcAction=playpause' in config.read_text(), 'MPC scope and action persist')
        choose(h, 110, 2)
        check('Найти по OK' in text(h, 103) and 'Пауза' in text(h, 107), 'Russian applies to controls and current status')
        check('Language=ru' in config.read_text(), 'manual language choice persists')
        capture(h, desktop.pid, '03-desktop-ru.png')
        click(h, 103)
        check('Ожидание кнопки OK' in text(h, 107), 'discovery starts with visible instructions')
        check(not u.IsWindowEnabled(u.GetDlgItem(h, 104)), 'mapping cannot be enabled during discovery')
        capture(h, desktop.pid, '04-discovery-ru.png')
        click(h, 103)
        check('Пауза' in text(h, 107), 'discovery can be cancelled')
        click(h, 113)
        check(bool(u.IsWindowVisible(u.GetDlgItem(h, 108))), 'diagnostics expand on demand')
        capture(h, desktop.pid, '05-diagnostics-ru.png')
        click(h, 113)
        check(not u.IsWindowVisible(u.GetDlgItem(h, 108)), 'diagnostics collapse again')
        u.PostMessageW(h, 0x10, 0, 0)
        check(wait_for(lambda: not u.IsWindowVisible(h)) and desktop.poll() is None, 'desktop close hides to tray and preserves process')
        kodi_config = profile / 'kodi.ini'
        kodi_config.write_text('[UI]\nLanguage=auto\n[Remote]\nEnabled=0\n', encoding='ascii')
        kodi = bridge.Helper(str(exe), str(kodi_config), os.getpid(), 'en')
        kh = wait_for(lambda: owned_window('G60S_OK_Kodi_' + str(os.getpid()), kodi.process.pid))
        check(kh and desktop.poll() is None, 'Kodi helper and desktop run together')
        check(not u.IsWindowVisible(kh), 'Kodi helper starts hidden')
        check(bridge.show_configuration(os.getpid()), 'Kodi configuration entry opens existing helper')
        check('Find by OK' in text(kh, 103), 'automatic Kodi language is inherited as English')
        check(not u.IsWindowEnabled(u.GetDlgItem(kh, 111)) and not u.IsWindowEnabled(u.GetDlgItem(kh, 112)), 'Kodi scope and action cannot become global or MPC')
        capture(kh, kodi.process.pid, '06-kodi-en.png')
        choose(kh, 110, 2)
        capture(kh, kodi.process.pid, '07-kodi-ru.png')
        u.PostMessageW(kh, 0x10, 0, 0)
        check(wait_for(lambda: not u.IsWindowVisible(kh)) and kodi.poll() is None, 'closing Kodi settings preserves its helper')
        original = kodi.process
        kodi.stop(); kodi = None
        check(original.returncode == 0, 'Kodi stop event exits gracefully while desktop stays alive')
        check(desktop.poll() is None, 'stopping Kodi does not terminate desktop helper')
        click(h, 106)
        check(desktop.wait(timeout=3) == 0, 'explicit desktop Exit closes the process')
        desktop = spawn(exe, ['--config', str(config)])
        h = wait_for(lambda: owned_window('G60S_OK_Desktop_2', desktop.pid))
        check(h and 'Найти по OK' in text(h, 103), 'language restored on restart')
        check(u.SendMessageW(u.GetDlgItem(h, 111), 0x147, 0, 0) == 1 and
              u.SendMessageW(u.GetDlgItem(h, 112), 0x147, 0, 0) == 1, 'MPC settings restored on restart')
        click(h, 106); desktop.wait(timeout=3)
        bad = spawn(exe, ['--kodi-pid', str(os.getpid())])
        check(bad.wait(timeout=3) == 4, 'incomplete Kodi arguments fail closed')
        parent = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)'], creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            kodi = bridge.Helper(str(exe), str(profile / 'watchdog.ini'), parent.pid, 'en')
            check(wait_for(lambda: owned_window('G60S_OK_Kodi_' + str(parent.pid), kodi.process.pid)), 'watchdog test helper starts for an isolated parent')
            parent.terminate(); parent.wait(timeout=3)
            check(wait_for(lambda: kodi.poll() == 0, seconds=3), 'parent death closes the Kodi helper automatically')
            kodi.stop(); kodi = None
        finally:
            if parent.poll() is None:
                parent.terminate(); parent.wait(timeout=3)
    finally:
        if kodi:
            kodi.stop()
        if desktop.poll() is None:
            if h:
                u.PostMessageW(h, 0x111, 106, 0)
            try:
                desktop.wait(timeout=3)
            except subprocess.TimeoutExpired:
                desktop.terminate(); desktop.wait(timeout=3)
        (WORK / 'results.json').write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding='utf-8')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', help='Test this executable, for example one extracted from a CI release ZIP')
    main(parser.parse_args().exe)
