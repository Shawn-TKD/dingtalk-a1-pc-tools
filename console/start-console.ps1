$ErrorActionPreference = 'Stop'
$appDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$workspace = Split-Path -Parent $appDir
$venvPython = Join-Path $workspace '.venv\Scripts\python.exe'
$codexPython = Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
$pythonArgs = @()

if (Test-Path -LiteralPath $venvPython) {
    $python = $venvPython
} elseif (Test-Path -LiteralPath $codexPython) {
    $python = $codexPython
} elseif ($pythonCommand = Get-Command python.exe -ErrorAction SilentlyContinue) {
    $python = $pythonCommand.Source
} elseif ($pyCommand = Get-Command py.exe -ErrorAction SilentlyContinue) {
    $python = $pyCommand.Source
    $pythonArgs = @('-3')
} else {
    throw 'Python 3.11+ was not found. Create .venv or install Python first.'
}
$config = Join-Path $workspace '.a1-device.json'
$tokenFile = Join-Path $workspace '.a1-console-token'
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

$accessToken = $env:A1_CONSOLE_TOKEN
if (-not $accessToken) {
    if (Test-Path -LiteralPath $tokenFile) {
        $accessToken = (Get-Content -Raw -LiteralPath $tokenFile).Trim()
    } else {
        $tokenBytes = New-Object byte[] 24
        $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
        try {
            $rng.GetBytes($tokenBytes)
        } finally {
            $rng.Dispose()
        }
        $accessToken = [Convert]::ToBase64String($tokenBytes).TrimEnd('=').Replace('+', '-').Replace('/', '_')
        [IO.File]::WriteAllText($tokenFile, $accessToken, [Text.Encoding]::ASCII)
    }
}
if ($accessToken.Length -lt 24) {
    throw "Console access token must contain at least 24 characters. Delete $tokenFile to regenerate it."
}

Set-Location -LiteralPath $appDir
& $python @pythonArgs '.\server.py' --config $config --host '0.0.0.0' --access-token $accessToken --open
