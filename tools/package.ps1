# Dong goi 59-eye: build -> gom file (deploy.py) -> bo cai Inno Setup -> latest.json cho OTA.
#   powershell -ExecutionPolicy Bypass -File tools\package.ps1
#   powershell -ExecutionPolicy Bypass -File tools\package.ps1 -Notes "Sua loi..." -Publish   (phat hanh len GitHub)
# Phien ban lay tu CMakeLists.txt: project(EYE59 VERSION x.y.z). Tang so nay truoc moi lan phat hanh.
param([string]$Notes = "", [string]$Repo = "phamchitai95/59-eye", [switch]$Publish)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$deps = Join-Path (Split-Path $root -Parent) '_deps'

$ver = [regex]::Match((Get-Content "$root\CMakeLists.txt" -Raw), 'project\(EYE59 VERSION ([\d\.]+)').Groups[1].Value
if (-not $ver) { throw "Khong doc duoc VERSION trong CMakeLists.txt" }
Write-Host "== 59-eye $ver =="

$updateUrl = if ($Repo) { "https://github.com/$Repo/releases/latest/download/latest.json" } else { "" }
# UPDATE_URL duoc nhung vao exe luc build (nguon OTA mac dinh)
& cmd /c "`"$root\build.bat`"" | Out-Null
if ($LASTEXITCODE -ne 0) { throw "build.bat loi" }
if ($updateUrl) {
    $cmake = Join-Path $deps 'venv\Scripts\cmake.exe'
    $env:PATH = "$deps\Qt\Tools\mingw1310_64\bin;$deps\Qt\6.8.3\mingw_64\bin;$deps\venv\Scripts;" + $env:PATH
    & $cmake -S $root -B "$root\build" "-DUPDATE_URL=$updateUrl" | Out-Null
    & $cmake --build "$root\build" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "build loi" }
}

python "$root\tools\deploy.py"
if ($LASTEXITCODE -ne 0) { throw "deploy.py loi" }

& "$deps\innosetup\ISCC.exe" /Q "/DAppVersion=$ver" "$root\installer\59-eye.iss"
if ($LASTEXITCODE -ne 0) { throw "ISCC loi" }
$setup = "$root\dist\59-eye-setup-$ver.exe"
$sha = (Get-FileHash $setup -Algorithm SHA256).Hash.ToLower()

$manifest = [ordered]@{ version = $ver; file = "59-eye-setup-$ver.exe"; sha256 = $sha; notes = $Notes }
# UTF-8 khong BOM
[IO.File]::WriteAllText("$root\dist\latest.json", ($manifest | ConvertTo-Json), (New-Object Text.UTF8Encoding $false))
"{0}  {1:N1} MB  sha256={2}" -f $setup, ((Get-Item $setup).Length / 1MB), $sha

if ($Publish) {
    # Can: gh auth login (mot lan). Repo phai public de may khach tai khong can dang nhap.
    gh release create "v$ver" $setup "$root\dist\latest.json" --repo $Repo --title "59-eye $ver" --notes ($(if ($Notes) { $Notes } else { "59-eye $ver" }))
    if ($LASTEXITCODE -ne 0) { throw "gh release loi" }
    Write-Host "Da phat hanh: https://github.com/$Repo/releases/tag/v$ver"
}
