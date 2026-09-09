param([string]$FxcPath = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe')
$ErrorActionPreference = 'Stop'
foreach ($name in @('GaussianHorizontal', 'LineMask', 'PointCleanup', 'Composite', 'ChromaField')) {
    $binary = Join-Path $PSScriptRoot "obj\Release\net10.0-windows10.0.19041.0\$name.cso"
    $assembly = (& $FxcPath /dumpbin $binary | Out-String)
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect $binary" }
    foreach ($semantic in @('SCENE_POSITION', 'TEXCOORD')) {
        if ($assembly -notmatch $semantic) { throw "$name has no $semantic input" }
    }
    $layout = switch ($name) {
        'GaussianHorizontal' { @(@('scale', 0), @('smallInverseSum', 4), @('largeInverseSum', 8), @('inputBounds', 16)) }
        'LineMask' { @(@('threshold', 0), @('invertLines', 4), @('scale', 8), @('positiveLobe', 12), @('smallInverseSum', 16), @('largeInverseSum', 20), @('strengthInfluence', 24), @('stability', 28), @('inputBounds', 32)) }
        'PointCleanup' { @(@('pointSize', 0), @('inputBounds', 16)) }
        'Composite' { @(@('amount', 0), @('contrast', 4), @('outputMode', 8), @('brightness', 12), @('hueShift', 16), @('sideMode', 20), @('colorBleed', 24)) }
        'ChromaField' { @() }
    }
        foreach ($entry in $layout) {
            $field, $offset = $entry
            if ($assembly -notmatch "float[1-4]?\s+$field;\s+// Offset:\s+$offset\s") {
                throw "$name constant $field is not at byte offset $offset"
            }
        }
        Write-Output "PASS: $name constant buffer layout"
    Write-Output "PASS: $name receives Direct2D coordinates"
}
