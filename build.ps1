$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'Visual Studio C++ x64 build tools are required.' }
$cmake = Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) { $cmake = (Get-Command cmake -ErrorAction Stop).Source }
$generator = if ($installation -match '\\18\\') { 'Visual Studio 18 2026' } else { 'Visual Studio 17 2022' }
Push-Location $PSScriptRoot
try {
    & $cmake -S . -B build -G $generator -A x64
    if ($LASTEXITCODE) { throw 'CMake configure failed.' }
    & $cmake --build build --config Release
    if ($LASTEXITCODE) { throw 'Build failed.' }
    $ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
    & $ctest --test-dir build -C Release --output-on-failure
    if ($LASTEXITCODE) { throw 'Tests failed.' }
    New-Item -ItemType Directory -Force dist | Out-Null
    Copy-Item -LiteralPath build\Release\arcdps_slashcommands.dll -Destination dist\arcdps_slashcommands.dll
    Copy-Item -LiteralPath README.md -Destination dist\README.md
    New-Item -ItemType Directory -Force dist\licenses | Out-Null
    Copy-Item -LiteralPath third_party\imgui\LICENSE.txt -Destination dist\licenses\imgui-LICENSE.txt
    if (Test-Path -LiteralPath third_party\minhook\LICENSE.txt) {
        Copy-Item -LiteralPath third_party\minhook\LICENSE.txt -Destination dist\licenses\minhook-LICENSE.txt
    }
    Write-Host 'Built and tested: dist\arcdps_slashcommands.dll'
} finally { Pop-Location }
