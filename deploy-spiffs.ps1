# ESP32 Swim Pacer Deploy Script - WORKING CONFIGURATION
# Use this script to deploy the exact working SPIFFS configuration
param(
    [string]$Port = "COM7"
)

Write-Host "=== ESP32 Swim Pacer Deployment - WORKING CONFIG ===" -ForegroundColor Cyan
Write-Host "Board: ESP32 Dev Module" -ForegroundColor Green
Write-Host "SPIFFS Partition: 0x290000 (1,441,792 bytes)" -ForegroundColor Green

# Tool paths
$esptoolPath = "C:\Users\$env:USERNAME\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\5.1.0\esptool.exe"
$mkspiffsPath = "C:\Users\$env:USERNAME\AppData\Local\Arduino15\packages\esp32\tools\mkspiffs\0.2.3\mkspiffs.exe"

Write-Host ""
Write-Host "Step 1: Creating SPIFFS image..." -ForegroundColor Yellow
Push-Location data
Remove-Item *.bin -ErrorAction SilentlyContinue -Force
& $mkspiffsPath -c . -p 256 -b 4096 -s 1441792 ..\spiffs_deploy.bin
Pop-Location

if (Test-Path spiffs_deploy.bin) {
    Write-Host "✓ SPIFFS image created" -ForegroundColor Green
} else {
    Write-Host "✗ Failed to create SPIFFS image" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Step 2: Uploading SPIFFS to 0x290000..." -ForegroundColor Yellow
& $esptoolPath --chip esp32 --port $Port --baud 921600 write_flash 0x290000 spiffs_deploy.bin

if ($LASTEXITCODE -eq 0) {
    Write-Host "✓ SPIFFS uploaded successfully" -ForegroundColor Green
} else {
    Write-Host "✗ SPIFFS upload failed" -ForegroundColor Red
    exit 1
}

Remove-Item spiffs_deploy.bin -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "=== DEPLOYMENT COMPLETE ===" -ForegroundColor Cyan
Write-Host "Now compile and upload sketch using Arduino IDE with 'ESP32 Dev Module'" -ForegroundColor Yellow
Write-Host ""
Write-Host "Expected results:" -ForegroundColor White
Write-Host "- SPIFFS mounted successfully" -ForegroundColor Gray
Write-Host "- 3 files found (script.js, style.css, swim-pacer.html)" -ForegroundColor Gray
Write-Host "- Web interface at http://192.168.4.1" -ForegroundColor Gray