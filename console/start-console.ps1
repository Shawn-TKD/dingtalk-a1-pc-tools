$ErrorActionPreference = 'Stop'
$appDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$workspace = Split-Path -Parent $appDir
$venvPython = Join-Path $workspace '.venv\Scripts\python.exe'
$codexPython = Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
$pythonArgs = @()

if (Test-Path -LiteralPath $venvPython) {
    $python = $venvPython
} elseif ($pythonCommand = Get-Command python.exe -ErrorAction SilentlyContinue) {
    $python = $pythonCommand.Source
} elseif ($pyCommand = Get-Command py.exe -ErrorAction SilentlyContinue) {
    $python = $pyCommand.Source
    $pythonArgs = @('-3')
} elseif (Test-Path -LiteralPath $codexPython) {
    $python = $codexPython
} else {
    throw 'Python 3.11+ was not found. Create .venv or install Python first.'
}
$config = Join-Path $workspace '.a1-device.json'
$localDependencies = Join-Path $workspace '.tools\python'
$frontend = Join-Path $appDir 'dist\index.html'

if (-not (Test-Path -LiteralPath $config)) {
    throw "Missing local device config: $config"
}
if (-not (Test-Path -LiteralPath $frontend)) {
    throw 'Frontend is not built. Run pnpm --dir console build first.'
}
if (Test-Path -LiteralPath $localDependencies) {
    $env:PYTHONPATH = if ($env:PYTHONPATH) {
        "$localDependencies;$env:PYTHONPATH"
    } else {
        $localDependencies
    }
}

Set-Location -LiteralPath $appDir
& $python @pythonArgs '.\server.py' --config $config --open
