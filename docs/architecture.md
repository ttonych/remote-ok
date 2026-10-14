# Architecture / Архитектура

The two packages contain the same `G60S-OK.exe`, compiled once from `src/app.cpp`.

| Source | Responsibility / Назначение |
| --- | --- |
| `src/hid.hpp` | Enumerate Consumer Control devices; decode Menu Pick and the exact G60S fallback reports; track press/release edges per report ID |
| `src/policy.hpp` | Decide between ignoring a press, yielding to Kodi, sending Enter or an MPC command |
| `src/session.hpp` | Per-session instance locks, compatibility with the old 1.0 lock and Kodi ownership markers |
| `src/app.cpp` | Win32 settings UI, Raw Input receiver, foreground checks, input delivery, settings and lifecycle |
| `addon/script.g60s.ok/` | Kodi Python service, configuration entry, native helper lifecycle and notification translations |

Windows Raw Input supplies HID Consumer Control reports to a separate process. Keyboard Raw Input is not registered. The selected device is matched by its exact path. Repeated down reports produce no extra action; release arms the next press. Learning pauses mapping and never injects an action. Device removal pauses delivery; reconnection resumes only for the saved path and enabled state.

Desktop and Kodi helpers use distinct singleton locks. A live Kodi helper exposes a marker containing its parent PID; the desktop helper checks that marker against the foreground PID before sending anything. Pausing the Kodi helper retains its priority. Exiting it releases priority. A process handle detects Kodi termination without relying on PID reuse, and a stop event permits graceful service shutdown. Incomplete Kodi arguments fail without entering global desktop mode.

The desktop helper identifies MPC by the exact executable basename and main window class. Other player windows retain Enter. Input delivery rechecks the foreground immediately before `SendInput` or the bounded `WM_COMMAND` call. It does not hook or suppress the remote's original HID event, so applications that already handle that event need mapping paused.

Настольная программа и дополнение используют один EXE. Каждое нажатие проходит декодирование HID, проверку начала нажатия, выбор режима и повторную проверку активного окна. Приоритет дополнения привязан к процессу Kodi, а не к заголовку окна. Неполные параметры запуска Kodi не могут включить общий режим. Старый протокол блокировки 1.0 сохранён для предотвращения совместного запуска с несовместимым обработчиком.

Settings remain local. The native diagnostic log is bounded in memory and contains Consumer device/report information only. The Kodi service writes its lifecycle and errors to Kodi's own log. No network calls, global keyboard hooks, driver installation, administrative elevation or telemetry are used by the helper.
