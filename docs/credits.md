# Technical sources / Технические источники

- [ClearCarbon's G60S Pro workaround](https://github.com/ClearCarbon/g60spro-ahk-enter-fix): public hardware findings and the known `01 41 00` / `01 00 00` reports. This project implements its own C++ handler; it does not bundle AutoHotkey or copy that script.
- [Microsoft Raw Input](https://learn.microsoft.com/en-us/windows/win32/inputdev/raw-input) and [HID parser routines](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/interpreting-hid-reports): device registration, descriptors and report decoding.
- [MPC-HC command definitions](https://github.com/clsid2/mpc-hc/blob/develop/src/mpc-hc/resource.h) and [MPC-BE command definitions](https://github.com/Aleksoid1978/MPC-BE/blob/master/src/apps/mplayerc/resource.h): command identifiers for playback and fullscreen. Player code and binaries are not included.
- [MPC-BE external control protocol](https://github.com/Aleksoid1978/MPC-BE/blob/master/src/apps/mplayerc/mpcapi.h) and [MPC-HC external control protocol](https://github.com/clsid2/mpc-hc/blob/develop/src/mpc-hc/MpcApi.h): playback notifications used by the optional integration test.
- [Kodi service add-ons](https://kodi.wiki/view/Service_add-ons) and [Kodi add-on metadata](https://kodi.wiki/view/Addon.xml): service lifetime and packaging conventions.

G60S OK is an independent community project. The package contains its own code and documentation under the MIT license, and the compiled Windows helper. Windows system libraries, Kodi and MPC players remain separate dependencies.
