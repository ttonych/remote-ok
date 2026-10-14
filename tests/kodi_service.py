"""Disposable integration harness loaded only by the isolated portable Kodi."""
import ctypes
from ctypes import wintypes
import json
import os
import time
import traceback
import xbmc
import xbmcvfs

report = {"version": xbmc.getInfoLabel("System.BuildVersion"), "checks": []}
path = xbmcvfs.translatePath("special://profile/g60s-integration.json")
u = ctypes.WinDLL("user32", use_last_error=True)
u.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
u.FindWindowW.restype = wintypes.HWND
u.IsWindowVisible.argtypes = [wintypes.HWND]
u.IsWindowVisible.restype = wintypes.BOOL
u.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]

def rpc(method, **params):
    result = json.loads(xbmc.executeJSONRPC(json.dumps({"jsonrpc": "2.0", "id": 1, "method": method, "params": params})))
    if "error" in result:
        raise RuntimeError(str(result))
    return result["result"]

def window():
    return u.FindWindowW("G60S_OK_Kodi_{}".format(os.getpid()), None)

def wait_for(predicate, seconds=12):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        if predicate():
            return True
        xbmc.sleep(100)
    return False

def check(condition, name):
    if not condition:
        raise AssertionError(name)
    report["checks"].append("PASS " + name)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(report, f, ensure_ascii=False, indent=2)

try:
    xbmc.sleep(2500)
    details = rpc("Addons.GetAddonDetails", addonid="script.g60s.ok", properties=["version", "enabled", "name"])
    check(details["addon"]["version"] == os.environ["G60S_TEST_VERSION"], "Kodi discovers bundled add-on and its version")
    rpc("Addons.SetAddonEnabled", addonid="script.g60s.ok", enabled=True)
    check(wait_for(window), "enabling add-on starts native HID helper")
    check(not u.IsWindowVisible(window()), "service starts with hidden configuration")
    rpc("Addons.ExecuteAddon", addonid="script.g60s.ok")
    check(wait_for(lambda: window() and u.IsWindowVisible(window())), "Programs entry opens configuration via Kodi Python")
    u.PostMessageW(window(), 0x10, 0, 0)
    check(wait_for(lambda: window() and not u.IsWindowVisible(window())), "configuration close keeps service running")
    rpc("Addons.SetAddonEnabled", addonid="script.g60s.ok", enabled=False)
    check(wait_for(lambda: not window()), "disabling add-on stops native helper")
    rpc("Addons.SetAddonEnabled", addonid="script.g60s.ok", enabled=True)
    check(wait_for(window), "re-enabling add-on starts a fresh helper")
    report["success"] = True
except Exception:
    report["error"] = traceback.format_exc()
finally:
    with open(path, "w", encoding="utf-8") as f:
        json.dump(report, f, ensure_ascii=False, indent=2)
    xbmc.executebuiltin("Quit")
