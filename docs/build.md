# Build and release / Сборка и релиз

## Local build / Локальная сборка

Windows x64, Git, Python 3.8+ and Visual Studio 2022 C++ x64 build tools with the Windows SDK are required. Python used by the optional local test runners should be 3.9+; the Kodi add-on is validated for Python 3.8 syntax.

```powershell
.\build.ps1
```

The script finds MSVC through `vswhere`, compiles with `/W4 /WX /MT`, runs the core tests and validates Kodi Python/XML/localizations. It packages both deliveries into `outputs/v1.1.0-local/` with SHA-256 checksums. The native executable is built once and reused in both ZIPs. `-NoPackage` stops after build and checks; `-BuildDirectory` changes the intermediate build folder.

Сборка выполняется локально тем же скриптом, который предназначен для GitHub Actions. Архивы `-local` не являются публичным релизом. Рабочие прототипы в других папках `outputs/` не перезаписываются.

## Interactive local checks / Локальные проверки интерфейса

Run in an unlocked desktop session, with old G60S utilities closed:

```powershell
.\build.ps1 -NoPackage -BuildIntegrationProbe
python tests/windows_integration.py
python tests/dispatch_integration.py
python tests/dpi_preview.py
```

The dispatch probe calls the production dispatch function but is a separate test executable excluded from the packages. It sends Enter only to its own isolated receiver and, when explicitly supplied, the disposable player windows. The optional player test uses silent generated audio:

```powershell
python tests/dispatch_integration.py --mpc-hc work/players/hc/mpc-hc64.exe --mpc-be work/players/be/mpc-be64.exe
```

Both players must be disposable copies with their own adjacent INI files before starting. Tests will not create a registry profile as a substitute. The paths above are examples; use the actual extraction paths.

`windows_integration.py` captures windows when `G60S_CAPTURE_HELPER` points to a compatible capture script accepting `--hwnd`, `--pid` and `--output`; it also detects the locally installed `windows-live-capture` skill. Captures require Pillow in that helper's Python environment. No capture tool is needed for the lifecycle assertions. Always visually inspect saved screenshots before claiming a UI check.

`dpi_preview.py` exercises 144/192-DPI layout calculations in the test executable. It does not change Windows display settings and does not prove real multi-monitor DPI transitions.

For actual Kodi integration, extract official Kodi distributions into `work/`, then:

```powershell
python tests/run_kodi_integration.py work/kodi-g60s/portable21 work/kodi-g60s/portable22
```

The runner refuses locations outside `work/`. It creates a temporary test service inside those copies, installs the add-on folder, starts Kodi with `-p`, verifies the service while a desktop helper runs, and quits Kodi. These copies are for testing only. This verifies folder installation and Python/service integration; it does not automate Kodi's ZIP-install dialog.

## Public release / Публичный релиз

1. Confirm the repository owner/name and review the source-only publication set. Exclude `outputs/`, `work/`, build folders, device settings and logs.
2. Record the physical remote checklist results in `testing.md`. For 1.1.0, publication without a physical remote check was explicitly approved; retain the untested-hardware notice in both READMEs and release notes. Set `VERSION`, `addon.xml`, locale headers and version-specific README names together.
3. Commit the reviewed source and create the matching `v1.1.0` tag. GitHub Actions must check out that exact tag and run `./build.ps1 -Release` on Windows.
4. Publish both resulting ZIPs and `SHA256SUMS.txt` only after build and checks succeed. Identify automatic tests and manual remote tests separately in release notes.

`-Release` requires GitHub Actions, `refs/tags/v<VERSION>` and a checkout matching `GITHUB_SHA`. The packaging step also rejects modified tracked source. Public release output goes into `dist/v<VERSION>/`. Never upload a locally built EXE or local ZIP as a public release asset or replace CI-built assets with local files.

Workflow `.github/workflows/build.yml` подготовлен локально: ветки и pull request проходят сборку/проверки без релизных пакетов; тег создаёт оба ZIP и SHA-256 одним вызовом `build.ps1 -Release`. Артефакт успешного запуска предназначен для публикации релиза без пересборки и замены файлов.

Исходники проекта: [ttonych/remote-ok](https://github.com/ttonych/remote-ok). Результаты выполнения workflow отображаются в [GitHub Actions](https://github.com/ttonych/remote-ok/actions). Наличие workflow само по себе не подтверждает успешную сборку. Публичный релиз требует отдельного тега, успешной сборки и честного описания выполненных и невыполненных проверок.
