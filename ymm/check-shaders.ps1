param([string]$FxcPath = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe')
$ErrorActionPreference = 'Stop'
foreach ($name in @('LineMask', 'Composite')) {
    $binary = Join-Path $PSScriptRoot "obj\Release\net10.0-windows10.0.19041.0\$name.cso"
    $assembly = (& $FxcPath /dumpbin $binary | Out-String)
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect $binary" }
    foreach ($semantic in @('SCENE_POSITION', 'TEXCOORD')) {
        if ($assembly -notmatch $semantic) { throw "$name has no $semantic input" }
    }
    if ($name -eq 'Composite') {
        foreach ($entry in @(
            @('amount', 0), @('contrast', 4), @('outputMode', 8), @('isLinear', 12),
            @('brightness', 16), @('hueShift', 20), @('vibrance', 24), @('padding', 28)
        )) {
            $field, $offset = $entry
            if ($assembly -notmatch "float\s+$field;\s+// Offset:\s+$offset\s") {
                throw "Composite constant $field is not at byte offset $offset"
            }
        }
        Write-Output 'PASS: Composite constant buffer layout is 32 bytes'
    }
    Write-Output "PASS: $name receives Direct2D coordinates"
}
