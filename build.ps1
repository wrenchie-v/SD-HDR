$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$cmake = Join-Path $root "tools\cmake-3.31.6-windows-x86_64\bin\cmake.exe"
if (-not (Test-Path $cmake)) {
    $cmake = "cmake"
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($install) {
        $vsdev = Join-Path $install "Common7\Tools\Launch-VsDevShell.ps1"
        if (Test-Path $vsdev) {
            & $vsdev -Arch amd64 -HostArch amd64 | Out-Null
        }
    }
}

& $cmake -S $root -B (Join-Path $root "build") -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmake --build (Join-Path $root "build") --config Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($args -contains "--self-test") {
    & (Join-Path $root "build\Release\SDHDR.exe") --self-test
    exit $LASTEXITCODE
}
if ($args -contains "--dump-caps") {
    & (Join-Path $root "build\Release\SDHDR.exe") --dump-caps
    exit $LASTEXITCODE
}
