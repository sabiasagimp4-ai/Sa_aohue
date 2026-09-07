param([string]$FxcPath = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe')
$ErrorActionPreference = 'Stop'
foreach ($name in @('LineMask', 'Composite')) {
    $binary = Join-Path $PSScriptRoot "obj\Release\net10.0-windows10.0.19041.0\$name.cso"
    $assembly = (& $FxcPath /dumpbin $binary | Out-String)
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect $binary" }
    foreach ($semantic in @('SCENE_POSITION', 'TEXCOORD')) {
        if ($assembly -notmatch $semantic) { throw "$name has no $semantic input" }
    }
    Write-Output "PASS: $name receives Direct2D coordinates"
}
