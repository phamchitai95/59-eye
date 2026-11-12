# Do do muot tren dau ghi that (dung config.json trong build\, khong doc/in mat khau).
# Mo 4 luong phu dau tien (2x2) trong N giay, ghi fps / khung rot cua tung luong.
param([int]$Latency = 200, [switch]$Leaky, [int]$Seconds = 40, [int]$Layout = 4, [string]$Tag = "", [string]$Login = "")
$g = 'D:\EYE\_deps\gst\PFiles64\gstreamer\1.0\mingw_x86_64'
$env:PATH = "$g\bin;D:\EYE\_deps\Qt\6.8.3\mingw_64\bin;D:\EYE\_deps\Qt\Tools\mingw1310_64\bin;$env:PATH"
Set-Location D:\EYE\litepss\build
$out = "D:\EYE\litepss\tools\testcfg\real_$Tag.txt"
$a = @('--layout', "$Layout", '--autofill', '--latency', "$Latency", '--quit-after', "$Seconds", '--stats', $out)
if ($Leaky) { $a += '--leaky' }
# 59-eye can dang nhap: -Login ten:matkhau (tai khoan trong users.json)
if ($Login) { $a += @('--login', $Login) }
$p = Start-Process D:\EYE\litepss\build\59-eye.exe -ArgumentList $a -PassThru
$p.WaitForExit(($Seconds + 30) * 1000) | Out-Null
"=== $Tag"; Get-Content $out
