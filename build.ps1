param([string]$SdkRoot=$env:AE_SDK_ROOT)
$ErrorActionPreference='Stop'
if (-not $SdkRoot) {
    $base=Split-Path $PSScriptRoot
    $SdkRoot=Join-Path $base 'AfterEffectsSDK_25.6_61_win/AfterEffectsSDK_25.6_61_win/ae25.6_61.64bit.AfterEffectsSDK'
}
if (-not (Test-Path (Join-Path $SdkRoot 'Examples/Headers/AE_Effect.h'))) { throw 'Set AE_SDK_ROOT to the installed Adobe SDK root.' }
$env:AE_SDK_ROOT=$SdkRoot
$helper=Join-Path $env:USERPROFILE '.agents/skills/ae-plugin-dev/scripts/build.ps1'
if (-not (Test-Path $helper)) { throw 'Use VS Developer Command Prompt: msbuild Win/Sa_aohue.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64' }
$result=& $helper -Project (Join-Path $PSScriptRoot 'Win/Sa_aohue.vcxproj') -OutDir (Join-Path $PSScriptRoot 'build')
$result
if (-not ($result | Where-Object { $_.Success -eq $true })) { throw 'Build failed; inspect the reported build log.' }
