# full-coop-sync task 0.2 (Windows): launch two ComboShip instances in one Anchor room.
#   .\scripts\coop\run-two-instances.ps1 [-AnchorHost 127.0.0.1] [-Port 43383] [-Room coop-test] [-Bin x64\Release] [-Data <dir with .o2r>]
# Each instance runs with its own working directory (ComboShip keeps saves + comboship.json in the CWD on
# Windows), seeded with the Anchor CVars so both connect on boot. Start a relay first if testing locally:
#   docker run -p 43383:43383 ghcr.io/garrettjoecox/anchor:latest
param(
    [string]$AnchorHost = "127.0.0.1",
    [int]$Port = 43383,
    [string]$Room = "coop-test",
    [string]$Bin = "x64\Release",
    [string]$Data = ""
)
$ErrorActionPreference = "Stop"
$Bin = (Resolve-Path $Bin).Path
if (-not $Data) { $Data = $Bin }
$Root = Join-Path (Get-Location) ".coop-instances"

function Launch($Name, $R, $G, $B) {
    $Home_ = Join-Path $Root $Name
    New-Item -ItemType Directory -Force -Path $Home_ | Out-Null
    # The launcher dlopens its modules from the exe dir; copy the runtime tree once per instance.
    if (-not (Test-Path (Join-Path $Home_ "ComboShip.exe"))) { Copy-Item -Recurse -Force "$Bin\*" $Home_ }
    foreach ($f in "oot.o2r", "oot-mq.o2r", "mm.o2r", "soh.o2r", "2ship.o2r") {
        $src = Join-Path $Data $f
        if ((Test-Path $src) -and -not (Test-Path (Join-Path $Home_ $f))) { Copy-Item $src $Home_ }
    }
    $cfg = Join-Path $Home_ "comboship.json"
    if (-not (Test-Path $cfg)) {
        @"
{ "CVars": { "gRemote": { "Anchor": {
    "Enabled": 1, "Host": "$AnchorHost", "Port": $Port, "RoomId": "$Room", "TeamId": "default",
    "Name": "$Name", "Color": { "Value": { "R": $R, "G": $G, "B": $B, "Type": "RGB" } },
    "Coop": { "Debug": 1 } } } } }
"@ | Set-Content -Encoding UTF8 $cfg
    }
    Start-Process -FilePath (Join-Path $Home_ "ComboShip.exe") -WorkingDirectory $Home_
    Write-Host "started $Name in $Home_"
}

Launch "Host" 100 255 100
Start-Sleep -Seconds 3   # the first client creates the room and becomes its owner (the seed host)
Launch "Joiner" 100 150 255
