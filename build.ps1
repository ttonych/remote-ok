param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'work\build'),
    [switch]$NoPackage,
    [switch]$Release,
    [switch]$BuildIntegrationProbe
)
$ErrorActionPreference = 'Stop'
$version = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'VERSION must contain major.minor.patch.' }
if ($Release) {
    if ($env:GITHUB_ACTIONS -ne 'true' -or $env:GITHUB_REF -ne "refs/tags/v$version") {
        throw 'Public release packages must be built by GitHub Actions from the matching version tag.'
    }
    $headCommit = & git -C $PSScriptRoot rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $headCommit -ne $env:GITHUB_SHA) { throw 'Checkout does not match the tagged workflow commit.' }
}
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Install Visual Studio 2022 C++ x64 build tools and Windows SDK.' }
$devCmdPath = Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
New-Item -ItemType Directory -Path $BuildDirectory -Force | Out-Null
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$versionComponents = $version.Replace('.', ',') + ',0'
$versionHeader = "#define G60S_VERSION_NUM $versionComponents`r`n#define G60S_VERSION_TEXT `"$version`"`r`n#define G60S_VERSION L`"$version`"`r`n"
Set-Content -LiteralPath (Join-Path $buildRoot 'build_version.h') -Value $versionHeader -Encoding ascii
$commands = @(
    '@echo off',
    ('call "' + $devCmdPath + '" -arch=x64 -host_arch=x64 >nul'),
    'if errorlevel 1 exit /b 1',
    ('rc /nologo /i "' + $buildRoot + '" /fo "' + (Join-Path $buildRoot 'app.res') + '" src\app.rc'),
    'if errorlevel 1 exit /b 1',
    ('cl /nologo /std:c++17 /O2 /MT /W4 /WX /utf-8 /EHsc /DUNICODE /D_UNICODE /FI"' + (Join-Path $buildRoot 'build_version.h') + '" src\app.cpp /Fo:"' + (Join-Path $buildRoot 'app.obj') + '" /Fe:"' + (Join-Path $buildRoot 'G60S-OK.exe') + '" /link /SUBSYSTEM:WINDOWS /MANIFEST:NO "' + (Join-Path $buildRoot 'app.res') + '" user32.lib shell32.lib gdi32.lib hid.lib ole32.lib comctl32.lib'),
    'if errorlevel 1 exit /b 1',
    ('cl /nologo /std:c++17 /O2 /MT /W4 /WX /utf-8 /EHsc /DUNICODE /D_UNICODE tests\core_tests.cpp /Fo:"' + (Join-Path $buildRoot 'core-tests.obj') + '" /Fe:"' + (Join-Path $buildRoot 'core-tests.exe') + '" /link user32.lib hid.lib'),
    'if errorlevel 1 exit /b 1'
)
$batchPath = Join-Path $buildRoot 'compile.cmd'
if ($BuildIntegrationProbe) {
    $commands += ('cl /nologo /std:c++17 /O2 /MT /W4 /WX /utf-8 /EHsc /DUNICODE /D_UNICODE /FI"' + (Join-Path $buildRoot 'build_version.h') + '" tests\dispatch_probe.cpp /Fo:"' + (Join-Path $buildRoot 'dispatch-probe.obj') + '" /Fe:"' + (Join-Path $buildRoot 'dispatch-probe.exe') + '" /link /MANIFEST:NO "' + (Join-Path $buildRoot 'app.res') + '" user32.lib shell32.lib gdi32.lib hid.lib ole32.lib comctl32.lib')
    $commands += 'if errorlevel 1 exit /b 1'
}
Set-Content -LiteralPath $batchPath -Value $commands -Encoding ascii
Push-Location $PSScriptRoot
try {
    & $env:ComSpec /d /c ('"' + $batchPath + '"')
    if ($LASTEXITCODE -ne 0) { throw 'C++ build failed.' }
    $builtVersion = (Get-Item -LiteralPath (Join-Path $buildRoot 'G60S-OK.exe')).VersionInfo.FileVersion
    if ($builtVersion -ne $version) { throw 'Executable version does not match VERSION.' }
    & (Join-Path $buildRoot 'core-tests.exe') | Tee-Object -FilePath (Join-Path $buildRoot 'core-results.txt')
    if ($LASTEXITCODE -ne 0) { throw 'Core tests failed.' }
    python scripts\validate.py
    if ($LASTEXITCODE -ne 0) { throw 'Add-on validation failed.' }
    if (-not $NoPackage) {
        $packageArguments = @('scripts\package.py', '--exe', (Join-Path $buildRoot 'G60S-OK.exe'))
        if ($Release) { $packageArguments += '--release' }
        python @packageArguments
        if ($LASTEXITCODE -ne 0) { throw 'Packaging failed.' }
    }
} finally { Pop-Location }
