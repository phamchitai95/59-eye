# Do LitePSS voi dau ghi gia lap: chay N giay, lay trung binh 10 giay cuoi tu file CSV.
# Dung: powershell -File bench.ps1 -Sink d3d11videosink -Layout 16 -Seconds 35
param([string]$Sink = "", [int]$Layout = 16, [int]$Seconds = 35, [switch]$NoFill, [string]$Shot = "", [string]$Login = "")
$g = 'D:\EYE\_deps\gst\PFiles64\gstreamer\1.0\mingw_x86_64'
$env:PATH = "$g\bin;D:\EYE\_deps\Qt\6.8.3\mingw_64\bin;D:\EYE\_deps\Qt\Tools\mingw1310_64\bin;$env:PATH"
$dir = 'D:\EYE\litepss\tools\testcfg'
$cfg = @"
{ "nvrs": [ { "name": "NVR gia lap", "host": "127.0.0.1", "httpPort": 8080, "rtspPort": 8554, "user": "", "password": "", "channels": 16 } ],
  "liveLatencyMs": 200, "playbackLatencyMs": 500, "videoSink": "$Sink" }
"@
[IO.File]::WriteAllText("$dir\config.json", $cfg)
Set-Location $dir
$before = Get-Date
$args2 = @('--layout', "$Layout", '--record', '--quit-after', "$Seconds")
if (-not $NoFill) { $args2 += '--autofill' }
if ($Shot) { $args2 += @('--screenshot', $Shot) }
# 59-eye can dang nhap: -Login ten:matkhau (tai khoan trong users.json)
if ($Login) { $args2 += @('--login', $Login) }
$p = Start-Process D:\EYE\litepss\build\59-eye.exe -ArgumentList $args2 -PassThru
$p.WaitForExit(($Seconds + 30) * 1000) | Out-Null
$csv = Get-ChildItem D:\EYE\litepss\build\metrics_*.csv | Where-Object LastWriteTime -gt $before | Sort-Object LastWriteTime | Select-Object -Last 1
$rows = Import-Csv $csv.FullName | Select-Object -Last 10
$avg = { param($c) [math]::Round(($rows | ForEach-Object { [double]($_.$c) } | Measure-Object -Average).Average, 1) }
[pscustomobject]@{
  sink = if ($Sink) { $Sink } else { 'auto' }; layout = $Layout; streams = $rows[-1].streams
  cpu = & $avg 'cpu'; private_mb = & $avg 'private_mb'; ws_mb = & $avg 'ws_mb'
  gpu_decode = & $avg 'gpu_decode'; gpu_3d = & $avg 'gpu_3d'; gpu_mem_mb = & $avg 'gpu_mem_mb'; net_mbps = & $avg 'net_mbps'
} | Format-Table -AutoSize | Out-String -Width 200
