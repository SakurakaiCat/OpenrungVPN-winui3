#requires -Version 5
# Build openrung-winui3 on Windows: Go core + WinUI3 app -> dist\
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

Write-Host '==> publishing OpenRung.WinUI (win-x64, self-contained)'
& dotnet publish (Join-Path $root 'app\OpenRung.WinUI\OpenRung.WinUI.csproj') `
    -c Release -r win-x64 --self-contained `
    -p:WindowsPackageType=None -p:WindowsAppSDKSelfContained=true `
    -p:PublishSingleFile=false `
    -o (Join-Path $root 'dist')
if ($LASTEXITCODE -ne 0) { throw 'dotnet publish failed' }

Write-Host "==> done: $root\dist"
