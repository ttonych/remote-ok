"""Windows helper lifecycle. Imported only on Windows; no Kodi dependencies."""
import ctypes
from ctypes import wintypes
import os
import subprocess
import uuid

kernel = ctypes.WinDLL("kernel32", use_last_error=True)
user = ctypes.WinDLL("user32", use_last_error=True)
kernel.CreateEventW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR]
kernel.CreateEventW.restype = wintypes.HANDLE
kernel.SetEvent.argtypes = [wintypes.HANDLE]
kernel.SetEvent.restype = wintypes.BOOL
kernel.CloseHandle.argtypes = [wintypes.HANDLE]
kernel.CloseHandle.restype = wintypes.BOOL
kernel.CreateMutexW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.LPCWSTR]
kernel.CreateMutexW.restype = wintypes.HANDLE
user.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
user.FindWindowW.restype = wintypes.HWND
user.ShowWindowAsync.argtypes = [wintypes.HWND, ctypes.c_int]
user.ShowWindowAsync.restype = wintypes.BOOL
user.SetForegroundWindow.argtypes = [wintypes.HWND]
user.SetForegroundWindow.restype = wintypes.BOOL
user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user.GetWindowThreadProcessId.restype = wintypes.DWORD
user.AllowSetForegroundWindow.argtypes = [wintypes.DWORD]
user.AllowSetForegroundWindow.restype = wintypes.BOOL


class ServiceLock:
    def __init__(self, kodi_pid):
        self.handle = kernel.CreateMutexW(None, False, "Local\\KodiG60S_Service_{}".format(kodi_pid))
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        self.acquired = ctypes.get_last_error() != 183  # ERROR_ALREADY_EXISTS

    def close(self):
        if self.handle:
            kernel.CloseHandle(self.handle)
            self.handle = None


class Helper:
    def __init__(self, executable, config_path, kodi_pid, language=None):
        self.process = None
        self.event = None
        self.event_name = "Local\\KodiG60S_Stop_" + uuid.uuid4().hex
        self.event = kernel.CreateEventW(None, True, False, self.event_name)
        if not self.event:
            raise ctypes.WinError(ctypes.get_last_error())
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
        try:
            arguments = [executable, "--kodi-pid", str(kodi_pid), "--config", config_path,
                         "--stop-event", self.event_name]
            if language in ("en", "ru"):
                arguments += ["--language", language]
            self.process = subprocess.Popen(
                arguments,
                cwd=os.path.dirname(config_path),
                startupinfo=startup,
                creationflags=subprocess.CREATE_NO_WINDOW,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                close_fds=True,
            )
        except Exception:
            self.stop()
            raise

    def poll(self):
        return self.process.poll() if self.process else None

    def stop(self):
        if self.event:
            kernel.SetEvent(self.event)
        if self.process:
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                # Terminate only the child process created by this object.
                self.process.terminate()
                self.process.wait(timeout=2)
            self.process = None
        if self.event:
            kernel.CloseHandle(self.event)
            self.event = None


def show_configuration(kodi_pid):
    window = user.FindWindowW("G60S_OK_Kodi_{}".format(kodi_pid), None)
    if not window:
        return False
    helper_pid = wintypes.DWORD()
    user.GetWindowThreadProcessId(window, ctypes.byref(helper_pid))
    user.AllowSetForegroundWindow(helper_pid.value)
    user.ShowWindowAsync(window, 9)  # SW_RESTORE
    user.SetForegroundWindow(window)
    return True
