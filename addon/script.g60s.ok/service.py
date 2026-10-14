"""Start the HID helper on login; stop it on add-on disable, logout or exit."""
import os
import struct
import sys

import xbmc
import xbmcaddon
import xbmcgui
import xbmcvfs


def main():
    addon = xbmcaddon.Addon()
    title = addon.getAddonInfo("name")
    monitor = xbmc.Monitor()
    if sys.platform != "win32" or struct.calcsize("P") != 8:
        xbmc.log("[G60S OK] Requires Windows x64 Kodi.", xbmc.LOGERROR)
        return
    from resources.bridge import Helper, ServiceLock

    lock = ServiceLock(os.getpid())
    helper = None
    try:
        if not lock.acquired:
            return
        root = xbmcvfs.translatePath(addon.getAddonInfo("path"))
        profile = xbmcvfs.translatePath(addon.getAddonInfo("profile"))
        os.makedirs(profile, exist_ok=True)
        executable = os.path.join(root, "resources", "bin", "win64", "G60S-OK.exe")
        config = os.path.join(profile, "remote.ini")
        if not os.path.isfile(executable):
            raise RuntimeError("Bundled G60S-OK.exe is missing; reinstall the add-on ZIP.")
        if not os.path.exists(config):
            xbmcgui.Dialog().notification(title, addon.getLocalizedString(32004), time=7000, sound=False)
        collision_notified = False
        while not monitor.abortRequested():
            language = "ru" if xbmc.getLanguage(xbmc.ISO_639_1).lower() == "ru" else "en"
            helper = Helper(executable, config, os.getpid(), language)
            xbmc.log("[G60S OK] Helper started, pid={}".format(helper.process.pid), xbmc.LOGINFO)
            while not monitor.waitForAbort(0.5):
                if helper.poll() is not None:
                    break
            if monitor.abortRequested():
                break
            code = helper.poll()
            helper.stop()
            helper = None
            if code == 3:
                if not collision_notified:
                    xbmcgui.Dialog().notification(title, addon.getLocalizedString(32005), time=9000, sound=False)
                    collision_notified = True
                # Older 1.0 helpers cannot share input ownership. The 1.1 desktop
                # and add-on helpers coexist and give Kodi input to this service.
                if monitor.waitForAbort(10):
                    break
                continue
            raise RuntimeError("HID helper exited unexpectedly (code {}).".format(code))
    except Exception as exc:
        xbmc.log("[G60S OK] {}".format(exc), xbmc.LOGERROR)
        if not monitor.abortRequested():
            xbmcgui.Dialog().notification(title, addon.getLocalizedString(32006), time=8000, sound=False)
    finally:
        if helper is not None:
            helper.stop()
        lock.close()
        xbmc.log("[G60S OK] Service stopped; helper closed.", xbmc.LOGINFO)


if __name__ == "__main__":
    main()
