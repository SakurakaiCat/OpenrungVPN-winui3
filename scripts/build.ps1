#requires -Version 5
# Build openrung-winui3 on Windows: Go core + C++/WinRT WinUI3 app -> dist\
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

Write-Host '==> building openrung-core.exe (windows/amd64)'
Push-Location (Join-Path $root 'core')
try {
    $env:CGO_ENABLED = '0'
    & go build -tags 'with_utls with_external_windivert' -trimpath -ldflags '-s -w' `
        -o (Join-Path $root 'dist\core\openrung-core.exe') .
    if ($LASTEXITCODE -ne 0) { throw 'go build failed' }
} finally { Pop-Location }

# Locate MSBuild through vswhere (VS 2019/2022 or Build Tools).
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found; install Visual Studio Build Tools (C++ + WinUI)' }
$msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild not found' }

$project = Join-Path $root 'app\OpenRung.WinUI.Cpp\OpenRung.WinUI.Cpp.vcxproj'
Write-Host '==> building OpenRung.WinUI (C++/WinRT, win-x64, self-contained)'
& $msbuild $project /restore /m /p:Configuration=Release /p:Platform=x64 `
    /p:OutDir="$root\dist\\" /p:IntDir="$root\app\OpenRung.WinUI.Cpp\obj\x64\Release\\"
$buildLog = $LASTEXITCODE
if ($buildLog -ne 0) { throw 'msbuild failed' }

Write-Host "==> done: $root\dist"
