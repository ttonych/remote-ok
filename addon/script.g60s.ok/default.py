"""Open the configuration UI owned by this Kodi session's service."""
import os
import struct
import sys

import xbmc
import xbmcaddon
import xbmcgui


def main():
    addon = xbmcaddon.Addon()
    title = addon.getAddonInfo("name")
    if len(sys.argv) > 1 and sys.argv[1] == "help":
        xbmcgui.Dialog().ok(title, addon.getLocalizedString(32008))
        return
    if sys.platform != "win32" or struct.calcsize("P") != 8:
        xbmcgui.Dialog().ok(title, addon.getLocalizedString(32007))
        return
    from resources.bridge import show_configuration

    monitor = xbmc.Monitor()
    for _ in range(20):
        if show_configuration(os.getpid()):
            return
        if monitor.waitForAbort(0.25):
            return
    xbmcgui.Dialog().ok(title, addon.getLocalizedString(32009))


if __name__ == "__main__":
    main()
