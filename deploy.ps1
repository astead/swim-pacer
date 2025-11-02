param(
    [string]$Port = "COM7"
)

# Get current time so we can show deployment duration
$startTime = Get-Date

# Tool paths
$esptoolPath = "C:\Users\$env:USERNAME\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\5.1.0\esptool.exe"
$mkspiffsPath = "C:\Users\$env:USERNAME\AppData\Local\Arduino15\packages\esp32\tools\mkspiffs\0.2.3\mkspiffs.exe"

# CRITICAL: Board configuration must match Arduino IDE$env:PATH += ";C:\Program Files\Arduino CLI"
$BoardFQBN = "esp32:esp32:esp32"  # ESP32 Dev Module

Write-Host "=== ESP32 Swim Pacer Deployment - WORKING CONFIG ===" -ForegroundColor Cyan
Write-Host "Board: ESP32 Dev Module" -ForegroundColor Green

Write-Host ""
Write-Host "=== STEP 1: Compile Sketch ===" -ForegroundColor Yellow
arduino-cli compile --fqbn $BoardFQBN --jobs 0 -v --optimize-for-debug --build-path ./build swim_pacer.ino

if ($LASTEXITCODE -eq 0) {
    Write-Host "Sketch compiled" -ForegroundColor Green
    $endTime = Get-Date
    $compileDuration = $endTime - $startTime
    Write-Host "Compilation time: $($compileDuration.Hours) hours, $($compileDuration.Minutes) minutes, $($compileDuration.Seconds) seconds" -ForegroundColor Green
} else {
    Write-Host "Compilation failed - use Arduino IDE instead" -ForegroundColor Yellow
    exit 1
}

Write-Host ""
Write-Host "=== STEP 2: Upload Sketch ===" -ForegroundColor Yellow
arduino-cli upload --fqbn $BoardFQBN --port $Port --build-path ./build -v -t swim_pacer.ino

if ($LASTEXITCODE -eq 0) {
    Write-Host "Sketch uploaded" -ForegroundColor Green
} else {
    Write-Host "Upload failed - use Arduino IDE instead" -ForegroundColor Yellow
    exit 1
}

Write-Host ""
Write-Host "=== DEPLOYMENT COMPLETE ===" -ForegroundColor Cyan
# Calculate and display duration in human readable format
$endTime = Get-Date
$totalDuration = $endTime - $startTime
$uploadDuration = $endTime - $startTime - $compileDuration
Write-Host "Upload time: $($uploadDuration.Hours) hours, $($uploadDuration.Minutes) minutes, $($uploadDuration.Seconds) seconds" -ForegroundColor Green
Write-Host "Total deployment time: $($totalDuration.Hours) hours, $($totalDuration.Minutes) minutes, $($totalDuration.Seconds) seconds" -ForegroundColor Green
Write-Host ""