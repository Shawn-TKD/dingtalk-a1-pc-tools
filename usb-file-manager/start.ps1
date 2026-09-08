param(
    [string]$Adb = 'C:\Users\20195\Documents\Codex\2026-08-16\https-github-com-awhsr15-dingtalk-a1\work\android-sdk\platform-tools\adb.exe',
    [string]$Credentials = 'C:\Users\20195\Documents\Codex\2026-08-16\https-github-com-awhsr15-dingtalk-a1\dingtalk-a1-pc-tools\.a1-device.json',
    [switch]$NoBrowser
)
$ErrorActionPreference = 'Stop'
$a1AppRoot = $PSScriptRoot
$a1Python = Join-Path (Split-Path $a1AppRoot -Parent) '.venv\Scripts\python.exe'
$a1Local = Join-Path $a1AppRoot '.local'
foreach ($a1Required in @($a1Python, $Adb, $Credentials, (Join-Path $a1AppRoot 'dist\index.html'))) {
    if (-not (Test-Path -LiteralPath $a1Required)) { throw "缺少文件：$a1Required" }
}
New-Item -ItemType Directory -Path $a1Local -Force | Out-Null
$a1Running = $false
$a1Existing = $null
try {
    $a1Existing = Invoke-WebRequest -Uri 'http://127.0.0.1:8766/' -UseBasicParsing -TimeoutSec 2
} catch { }
if ($null -ne $a1Existing) {
    if ($a1Existing.Content -notmatch '<title>A1 文件空间</title>') { throw '8766 端口不是 A1 文件空间' }
    $a1Running = $true
}
if (-not $a1Running) {
    $a1Args = @('-u', ('"'+(Join-Path $a1AppRoot 'server.py')+'"'), '--adb', ('"'+$Adb+'"'), '--credentials', ('"'+$Credentials+'"'))
    $a1Process = Start-Process -FilePath $a1Python -ArgumentList $a1Args -WorkingDirectory $a1AppRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $a1Local 'server.log') -RedirectStandardError (Join-Path $a1Local 'server-error.log')
    for ($a1Attempt=0; $a1Attempt -lt 20; $a1Attempt++) {
        Start-Sleep -Milliseconds 250
        if ($a1Process.HasExited) { throw '本地服务启动失败，请查看 .local/server-error.log' }
        try { $null = Invoke-WebRequest -Uri 'http://127.0.0.1:8766/' -UseBasicParsing -TimeoutSec 1; $a1Running=$true; break } catch { }
    }
    if (-not $a1Running) { throw '服务尚未就绪，请稍后重试' }
}
$a1Token = (Get-Content -LiteralPath (Join-Path $a1Local 'access-token.txt') -Raw).Trim()
$a1Url = 'http://127.0.0.1:8766/#token='+$a1Token
Write-Host "A1 文件空间：$a1Url"
if (-not $NoBrowser) { Start-Process $a1Url }
