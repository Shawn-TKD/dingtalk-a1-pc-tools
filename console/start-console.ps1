$ErrorActionPreference = 'Stop'
$appDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$workspace = Split-Path -Parent $appDir
$venvPython = Join-Path $workspace '.venv\Scripts\python.exe'
$python = if (Test-Path -LiteralPath $venvPython) {
    $venvPython
} else {
    (Get-Command python -ErrorAction Stop).Source
}
$config = Join-Path $workspace '.a1-device.json'

if (-not (Test-Path -LiteralPath $config)) {
    throw "Missing local device config: $config"
}

Set-Location -LiteralPath $appDir
& $python '.\server.py' --config $config --open
