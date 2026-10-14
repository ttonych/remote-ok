"""Exercise the shipping DispatchOK function with isolated windows and optional portable players."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import subprocess
import time
import wave

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / 'work/dispatch'
WORK.mkdir(parents=True, exist_ok=True)
PROBE = ROOT / 'work/build/dispatch-probe.exe'
u, k = c.WinDLL('user32', use_last_error=True), c.WinDLL('kernel32', use_last_error=True)
CALLBACK = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
u.EnumWindows.argtypes = [CALLBACK, w.LPARAM]
u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]; u.GetWindowThreadProcessId.restype = w.DWORD
u.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
u.GetForegroundWindow.restype = w.HWND
u.SetForegroundWindow.argtypes = [w.HWND]
u.ShowWindow.argtypes = [w.HWND, c.c_int]
u.BringWindowToTop.argtypes = [w.HWND]
u.SetFocus.argtypes = [w.HWND]
u.PeekMessageW.argtypes = [c.POINTER(w.MSG), w.HWND, w.UINT, w.UINT, w.UINT]
u.AttachThreadInput.argtypes = [w.DWORD, w.DWORD, w.BOOL]
u.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]; u.SendMessageW.restype = w.LPARAM
u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.GetMenu.argtypes = [w.HWND]; u.GetMenu.restype = w.HMENU
u.GetMenuState.argtypes = [w.HMENU, w.UINT, w.UINT]; u.GetMenuState.restype = w.UINT
u.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
k.CreateEventW.argtypes = [c.c_void_p, w.BOOL, w.BOOL, w.LPCWSTR]; k.CreateEventW.restype = w.HANDLE
k.CloseHandle.argtypes = [w.HANDLE]
results = []

def wait_for(predicate, seconds=8):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        value = predicate()
        if value:
            return value
        time.sleep(.05)
    raise AssertionError('Timed out waiting for test state')

def check(condition, name):
    if not condition:
        raise AssertionError(name)
    results.append('PASS ' + name)
    print(results[-1], flush=True)

def window(pid, class_name):
    found = []
    @CALLBACK
    def visit(h, _):
        actual = w.DWORD(); name = c.create_unicode_buffer(256)
        u.GetWindowThreadProcessId(h, c.byref(actual)); u.GetClassNameW(h, name, len(name))
        if actual.value == pid and name.value == class_name:
            found.append(h)
        return True
    u.EnumWindows(visit, 0)
    return found[0] if found else None

def focus(h):
    if u.GetForegroundWindow() == h:
        return
    msg = w.MSG(); u.PeekMessageW(c.byref(msg), None, 0, 0, 0)
    foreground = u.GetForegroundWindow()
    thread = u.GetWindowThreadProcessId(foreground, None) if foreground else 0
    current = k.GetCurrentThreadId()
    if thread and thread != current:
        u.AttachThreadInput(current, thread, True)
    try:
        u.ShowWindow(h, 9); u.BringWindowToTop(h); u.SetForegroundWindow(h)
    finally:
        if thread and thread != current:
            u.AttachThreadInput(current, thread, False)
    wait_for(lambda: u.GetForegroundWindow() == h)
    time.sleep(.2)

def dispatch(h, scope='all', action='enter', kodi_pid=0):
    focus(h)
    result = subprocess.run([str(PROBE), str(h), scope, action, str(kodi_pid)],
                            creationflags=subprocess.CREATE_NO_WINDOW, capture_output=True, text=True, check=True)
    return result.stdout.strip()

def counts(h):
    return tuple(u.SendMessageW(h, 0x8000 + i, 0, 0) for i in (10, 11, 12))

def close(process, h):
    if h:
        u.PostMessageW(h, 0x10, 0, 0)
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.terminate(); process.wait(timeout=3)

def receiver_checks():
    receiver = subprocess.Popen([str(PROBE), 'receiver'], creationflags=subprocess.CREATE_NO_WINDOW)
    h = None
    try:
        h = wait_for(lambda: window(receiver.pid, 'G60S_OK_TestReceiver'))
        check(dispatch(h) == 'policy=2 sent=1', 'desktop dispatch sends Enter to the active external test window')
        wait_for(lambda: counts(h) == (1, 1, 1))
        check(counts(h) == (1, 1, 1), 'exactly one Enter down, up and character arrive')
        check(dispatch(h, scope='mpc') == 'policy=0 sent=0' and counts(h) == (1, 1, 1), 'MPC-only mode sends nothing to another app')
        marker = k.CreateEventW(None, True, True, 'Local\\G60S_OK_KodiPriority_' + str(receiver.pid))
        try:
            check(bool(marker), 'isolated Kodi priority marker created')
            check(dispatch(h) == 'policy=1 sent=0' and counts(h) == (1, 1, 1), 'desktop yields to the Kodi priority marker without injecting')
            check(dispatch(h, kodi_pid=receiver.pid) == 'policy=2 sent=1', 'Kodi helper sends Enter only to its owning process')
            wait_for(lambda: counts(h) == (2, 2, 2))
            check(dispatch(h, kodi_pid=k.GetCurrentProcessId()) == 'policy=0 sent=0' and counts(h) == (2, 2, 2), 'Kodi helper blocks input to a different foreground process')
        finally:
            k.CloseHandle(marker)
        check(dispatch(h) == 'policy=2 sent=1', 'desktop resumes after Kodi priority is released')
        wait_for(lambda: counts(h) == (3, 3, 3))
    finally:
        close(receiver, h)

def rect(h):
    r = w.RECT(); u.GetWindowRect(h, c.byref(r))
    return (r.left, r.top, r.right, r.bottom)

def player_checks(path, class_name):
    exe = Path(path).resolve()
    # Require an existing portable INI: never write the user's registry profile.
    if not exe.with_suffix('.ini').exists():
        raise AssertionError('Create a disposable portable player with an INI before testing')
    silent = WORK / 'silence.wav'
    with wave.open(str(silent), 'wb') as audio:
        audio.setnchannels(1); audio.setsampwidth(2); audio.setframerate(8000)
        audio.writeframes(b'\0\0' * 8000 * 120)
    receiver = subprocess.Popen([str(PROBE), 'receiver'], creationflags=subprocess.CREATE_NO_WINDOW)
    receiver_window = wait_for(lambda: window(receiver.pid, 'G60S_OK_TestReceiver'))
    process = subprocess.Popen([str(exe), '/slave', str(receiver_window), str(silent), '/play'], cwd=exe.parent)
    h = None
    try:
        h = wait_for(lambda: window(process.pid, class_name))
        # Read the player's own /slave playback notifications through the receiver.
        def state():
            return u.SendMessageW(receiver_window, 0x8000 + 13, 0, 0)
        wait_for(lambda: state() == 0, 15)
        check(dispatch(h, 'mpc', 'playpause') == 'policy=3 sent=1', exe.name + ': dispatch accepts playback command')
        wait_for(lambda: state() == 1)
        check(state() == 1, exe.name + ': actual playback changes to paused')
        check(dispatch(h, 'mpc', 'playpause') == 'policy=3 sent=1', exe.name + ': second playback command delivered')
        wait_for(lambda: state() == 0)
        before = rect(h)
        check(dispatch(h, 'mpc', 'fullscreen') == 'policy=4 sent=1', exe.name + ': fullscreen command delivered')
        wait_for(lambda: rect(h) != before)
        check(rect(h) != before, exe.name + ': actual window enters fullscreen')
        dispatch(h, 'mpc', 'fullscreen')
        wait_for(lambda: rect(h) == before)
        check(rect(h) == before, exe.name + ': second press restores the original window')
    finally:
        close(process, h)
        close(receiver, receiver_window)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mpc-hc'); parser.add_argument('--mpc-be')
    args = parser.parse_args()
    try:
        receiver_checks()
        if args.mpc_hc:
            player_checks(args.mpc_hc, 'MediaPlayerClassicW')
        if args.mpc_be:
            player_checks(args.mpc_be, 'MPC-BE')
    finally:
        (WORK / 'results.json').write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding='utf-8')
