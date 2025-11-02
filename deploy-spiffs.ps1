# ESP32 Swim Pacer Deploy Script - WORKING CONFIGURATION
# Use this script to deploy the exact working SPIFFS configuration
param(
    [string]$Port = "COM7"
)

# Get current time so we can show deployment duration
$startTime = Get-Date

Write-Host "=== ESP32 Swim Pacer Deployment - WORKING CONFIG ===" -ForegroundColor Cyan
Write-Host "Board: ESP32 Dev Module" -ForegroundColor Green
Write-Host "SPIFFS Partition: 0x290000 (1,441,792 bytes)" -ForegroundColor Green

# Tool paths
$esptoolPath = "C:\Users\$env:USERNAME\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\5.1.0\esptool.exe"
$mkspiffsPath = "C:\Users\$env:USERNAME\AppData\Local\Arduino15\packages\esp32\tools\mkspiffs\0.2.3\mkspiffs.exe"

Write-Host ""
Write-Host "Step 1: Creating SPIFFS image..." -ForegroundColor Yellow
Remove-Item *.bin -ErrorAction SilentlyContinue -Force
& $mkspiffsPath -c data -p 256 -b 4096 -s 1441792 ./build/spiffs_deploy.bin

if (Test-Path ./build/spiffs_deploy.bin) {
    Write-Host "SPIFFS image created" -ForegroundColor Green
} else {
    Write-Host "Failed to create SPIFFS image" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Step 2: Erase SPIFFS on the board..." -ForegroundColor Yellow
& $esptoolPath --chip esp32 --port $Port --baud 921600 erase-region 0x290000 0x160000

if ($LASTEXITCODE -eq 0) {
    Write-Host "SPIFFS erased successfully" -ForegroundColor Green
} else {
    Write-Host "SPIFFS erase failed" -ForegroundColor Red
    exit 1
}


Write-Host ""
Write-Host "Step 3: Uploading SPIFFS to 0x290000..." -ForegroundColor Yellow
& $esptoolPath --chip esp32 --port $Port --baud 921600 write-flash 0x290000 ./build/spiffs_deploy.bin

if ($LASTEXITCODE -eq 0) {
    Write-Host "SPIFFS uploaded successfully" -ForegroundColor Green
} else {
    Write-Host "SPIFFS upload failed" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "=== DEPLOYMENT OF SPIFFS COMPLETE ===" -ForegroundColor Cyan
$endTime = Get-Date
$totalDuration = $endTime - $startTime
Write-Host "SPIFFS time: $($totalDuration.Hours) hours, $($totalDuration.Minutes) minutes, $($totalDuration.Seconds) seconds" -ForegroundColor Green
Write-Host ""
