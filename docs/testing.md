# Проверки 1.1.0 / Version 1.1.0 test report

Date: 2026-10-14. Environment: Windows 10 x64 build 19045, MSVC 2022, Python 3.13 for local runners. No physical G60S remote was available during these tests.

| Проверка / Check | Result |
| --- | --- |
| Native build, `/W4 /WX` | Passed; x64, static C++ runtime, embedded version and DPI manifest |
| HID decoding, report bounds, edge detection, routing and locks | 29 automatic checks passed |
| Actual foreground delivery and Kodi priority | 8 checks passed with an isolated receiver; exactly one Enter down/up/character per accepted dispatch |
| MPC-HC 2.8.3 x64 | 6 checks passed: actual play/pause/resume and fullscreen/restore, verified through playback notifications and window geometry |
| MPC-BE 1.9.2 x64 | Same 6 checks passed |
| Desktop/Kodi helper lifecycle and settings UI | 26 checks passed, including language persistence, mode persistence, coexistence, hidden startup, graceful stop, invalid arguments and parent-death watchdog |
| Kodi 21.3, `21.3.0`, Git `20251031-a3a448d26b` | 7 actual in-Kodi service checks passed; desktop remained alive, helper closed after Kodi quit |
| Kodi 22 RC1, `21.90.901`, Git `20260927-28ea2eac1eb` | Same 7 checks passed |
| English and Russian UI | Fresh native-window screenshots inspected; discovery and diagnostics included |
| Simulated 150% / 200% layout | See UI review; this is a layout simulation, not a change to Windows monitor scaling |
| Kodi source metadata | Python 3.8 syntax, XML, matching versions and locale ID coverage validated |
| Package integrity | Explicit file allowlists, x64 PE checks, ZIP CRC/member validation, identical helper in both ZIPs and SHA-256 output |
| Tagged release build | [Actions run 38559100603](https://github.com/ttonych/remote-ok/actions/runs/38559100603) passed for `v1.1.0`, commit `92bfb51924d2d0b11400b42ae9c65bcabeececcb`; both ZIPs were published unchanged from this run |
| Downloaded CI executable | All 26 local lifecycle/UI assertions passed against the executable extracted from the release artifact; uploaded asset digests match the CI files |

The MPC test calls the shipping dispatch implementation from a separate test executable. It does not synthesize a physical HID report. Kodi was run in disposable portable profiles; the add-on folder was installed directly. The ZIP-install UI has not been exercised. Kodi 22 final and Windows 11 have not been tested.

Автоматические проверки не заменяют проверку пульта. Пользователь подтвердил работу прежнего прототипа; для 1.1.0 ещё не проверены физические USB/Bluetooth-нажатия, переподключение и конкретная прошивка пульта. Переход между мониторами с разным DPI и экранный диктор также не проверены.

## Отложенная проверка на физическом пульте

Публикация 1.1.0 без физического пульта явно разрешена 2026-10-14. Пункты ниже остаются невыполненными; разрешение на публикацию не заменяет результаты проверки. / Publication of 1.1.0 without a physical remote test was explicitly approved on 2026-10-14. The following checks remain incomplete.

- [ ] USB: найти пульт, отпустить OK, включить; десять отдельных нажатий дают ровно десять действий. Удержание не повторяет действие.
- [ ] Bluetooth, если используется: повторить поиск и проверку отдельных нажатий.
- [ ] В общем режиме проверить нужное приложение; в режиме только MPC другие приложения не реагируют.
- [ ] В MPC проверить Enter, воспроизведение/паузу и полный экран; в диалоге плеера остаётся Enter.
- [ ] Запустить GUI и дополнение одновременно: в Kodi одно действие на нажатие; пауза дополнения прекращает переназначение в Kodi.
- [ ] Отключить и подключить тот же пульт; проверить восстановление. После перезапуска программ проверить сохранённые настройки.
- [ ] Установить итоговый ZIP через интерфейс Kodi и проверить запуск/остановку дополнения.

Record the remote connection type, firmware if known, Windows/Kodi/player versions and results before claiming those combinations are supported by manual testing.
