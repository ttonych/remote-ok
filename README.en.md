# Remote OK

[Русский](README.md)

Make the **OK** button on a G60S Pro remote work on Windows. The desktop app and Kodi add-on share the same native HID handler. No AutoHotkey, extra driver or Visual C++ Runtime installation is required.

The project name leaves room for more remotes. **G60S Pro** is the first supported model; other models are not yet supported. The current app and add-on keep the G60S OK name.

**Version 1.1.0 is undergoing local testing.** Archives ending in `-local` are test builds. The new version still needs testing with a physical remote; the user confirmed the previous prototype worked. Public release EXEs and ZIPs will be built only in GitHub Actions from the version tag.

![English desktop interface](docs/images/desktop-en.png)

## Choose a package

| Package | Purpose |
| --- | --- |
| `G60S-OK-Windows-x64-1.1.0[-local].zip` | OK as Enter in Windows applications, plus a dedicated MPC-BE / MPC-HC mode |
| `script.g60s.ok-1.1.0[-local].zip` | OK as Enter in the active Kodi process; starts and stops with Kodi |

Requires Windows 10/11 x64 and a local desktop session. The add-on targets Kodi 21/22 for Windows x64. See the [test report](docs/testing.md) for versions actually tested and remaining coverage.

## Desktop app

1. Exit the older 1.0 utility and any AutoHotkey script remapping this button. Extract the ZIP and run `G60S-OK.exe`.
2. Connect the USB receiver or Bluetooth remote. Turn off air mouse mode on the remote.
3. Click **Find by OK**, then press OK on the remote and release it. Discovery selects the device without enabling mapping.
4. Choose **All applications** or **MPC-BE / MPC-HC only**. Select Enter, play/pause or fullscreen for MPC. Player dialogs keep Enter.
5. Click **Enable**, switch to the target app and test OK. Holding the button does not repeat the action. Separate long-press and double-press actions are not implemented.

In All applications mode, the MPC action applies to the player's main window; other apps receive Enter. MPC-only mode ignores other apps. Keep the standard player executable name: `mpc-hc.exe`, `mpc-hc64.exe`, `mpc-be.exe` or `mpc-be64.exe`.

The window close button and **Hide to tray** keep the app running. Double-click its tray icon to reopen settings. **Exit** stops the app. Windows startup is not configured automatically; a shortcut with `--tray` starts it in the tray.

## Kodi add-on

1. In Kodi, open **Add-ons → Install from zip file** and select `script.g60s.ok-…zip`. Allow unknown sources if Kodi asks. Do not extract the archive.
2. Open **Program add-ons → G60S OK for Kodi**. Its native Windows settings window appears.
3. Turn off air mouse mode, click **Find by OK**, press and release OK, then click **Enable → Return to Kodi**.

![Kodi settings in English](docs/images/kodi-en.png)

The device and enabled state are saved. The helper starts automatically with Kodi and only acts while a window belonging to that Kodi process is active. Closing settings returns to Kodi. Disabling or removing the add-on stops its helper. Do not start the bundled EXE separately.

Both **1.1** packages can run together. The add-on takes priority inside Kodi, including while its mapping is paused. Once the add-on is disabled, the desktop app's All applications mode can handle Kodi again. Version 1.0 does not support coexistence: stop older copies before upgrading.

## Language and settings

The **Language** list applies Automatic, English or Russian immediately and saves the choice. Automatic uses the Windows UI language; in the add-on it uses Kodi's language when the helper starts.

Desktop settings are stored in `%LOCALAPPDATA%\G60S-OK\settings.ini`. Add-on settings are in the active Kodi profile's `addon_data/script.g60s.ok/remote.ini`. When a remote disconnects, the app waits and resumes an enabled mapping only for the same device path. A different USB port or renewed Bluetooth pairing may require discovery again.

## Troubleshooting

- Check air mouse mode, click **Refresh**, then **Find by OK**. Discovery can be cancelled and times out after 30 seconds.
- Check that a remote is selected, mapping is enabled, the target app is active and the chosen scope includes it.
- Exit old G60S OK copies and other tools remapping the same button. This app adds Enter without suppressing the original HID event. An app that already handles Menu Pick may therefore act twice; pause mapping there or select MPC-only mode.
- Windows may block input to apps running with higher privileges. Normally, run both apps without administrator rights.
- **Show diagnostics** displays an in-memory Consumer HID log. It does not capture typed text, upload data or write the log to disk. **Copy diagnostics** explicitly copies it to the clipboard. Review device names and identifiers before sharing a copied log.

The decoder supports HID Consumer Menu Pick (`0x0C / 0x41`) and the known three-byte G60S reports. Other firmware or remote variants may use different reports. Bluetooth and physical reconnection have not yet been tested with this version. This is not remote firmware or a driver; other buttons are not remapped.

## Development

See [building](docs/build.md), [architecture](docs/architecture.md), [testing](docs/testing.md), the [UI review](docs/ui-review.md) and [roadmap](roadmap.md). Licensed under [MIT](LICENSE). [Technical sources and credits](docs/credits.md).
