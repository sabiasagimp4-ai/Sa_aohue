$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot)
try {
    New-Item -ItemType Directory -Force build | Out-Null
    $vs=& 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' -latest -property installationPath
    $line='call "'+$vs+'\VC\Auxiliary\Build\vcvars64.bat" && cl /nologo /std:c++17 /EHsc /O2 /LD tests\core_bridge.cpp /Febuild\core_bridge.dll /Fobuild\core_bridge.obj'
    cmd /c $line
    if ($LASTEXITCODE -ne 0) { throw 'Core compile failed' }
    python tests/check_native.py
    if ($LASTEXITCODE -ne 0) { throw 'Native check failed' }
    if (-not $env:AE_SDK_ROOT) { $env:AE_SDK_ROOT=Join-Path (Split-Path (Get-Location)) 'AfterEffectsSDK_25.6_61_win/AfterEffectsSDK_25.6_61_win/ae25.6_61.64bit.AfterEffectsSDK' }
    $line='call "'+$vs+'\VC\Auxiliary\Build\vcvars64.bat" && cl /nologo /std:c++17 /EHsc /O2 /D MSWindows /D WIN32 /D _WINDOWS /D NOMINMAX /I "%AE_SDK_ROOT%/Examples/Headers" /I "%AE_SDK_ROOT%/Examples/Headers/SP" /I "%AE_SDK_ROOT%/Examples/Util" tests/check_ae.cpp /Febuild/check_ae.exe /Fobuild/check_ae.obj && build\check_ae.exe'
    cmd /c $line
    if ($LASTEXITCODE -ne 0) { throw 'AE adapter check failed' }
} finally { Pop-Location }
