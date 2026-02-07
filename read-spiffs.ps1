# ESP32 Swim Pacer - Read SPIFFS from Device
# This script reads the SPIFFS partition from a connected ESP32 and extracts files
param(
    [string]$Port = "COM7",
    [string]$OutputFolder = "SPIFFS-data"
)

# Get current time so we can show duration
$startTime = Get-Date

Write-Host "=== ESP32 Swim Pacer - Read SPIFFS ===" -ForegroundColor Cyan
Write-Host "Board: ESP32 Dev Module" -ForegroundColor Green
Write-Host "SPIFFS Partition: 0x290000 (1,441,792 bytes / 0x160000)" -ForegroundColor Green
Write-Host "Output Folder: $OutputFolder" -ForegroundColor Green

# Tool paths
$esptoolPath = "C:\Users\$env:USERNAME\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\5.1.0\esptool.exe"
$mkspiffsPath = "C:\Users\$env:USERNAME\AppData\Local\Arduino15\packages\esp32\tools\mkspiffs\0.2.3\mkspiffs.exe"

# Verify tools exist
if (-not (Test-Path $esptoolPath)) {
    Write-Host "Error: esptool not found at $esptoolPath" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $mkspiffsPath)) {
    Write-Host "Error: mkspiffs not found at $mkspiffsPath" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Step 1: Clearing output folder..." -ForegroundColor Yellow
if (Test-Path $OutputFolder) {
    Remove-Item -Path $OutputFolder -Recurse -Force
    Write-Host "Cleared existing $OutputFolder folder" -ForegroundColor Green
}
New-Item -ItemType Directory -Path $OutputFolder -Force | Out-Null
Write-Host "Created fresh $OutputFolder folder" -ForegroundColor Green

Write-Host ""
Write-Host "Step 2: Reading SPIFFS partition from ESP32 on $Port..." -ForegroundColor Yellow
$tempBinFile = "spiffs_read.bin"
& $esptoolPath --chip esp32 --port $Port --baud 921600 read_flash 0x290000 0x160000 $tempBinFile

if ($LASTEXITCODE -ne 0 -or -not (Test-Path $tempBinFile)) {
    Write-Host "Failed to read SPIFFS from device" -ForegroundColor Red
    exit 1
}

$binSize = (Get-Item $tempBinFile).Length
Write-Host "SPIFFS partition read successfully ($binSize bytes)" -ForegroundColor Green

Write-Host ""
Write-Host "Step 3: Extracting files from SPIFFS image..." -ForegroundColor Yellow
& $mkspiffsPath -u $OutputFolder -p 256 -b 4096 -s 1441792 $tempBinFile

if ($LASTEXITCODE -ne 0) {
    Write-Host "Failed to extract SPIFFS files" -ForegroundColor Red
    exit 1
}

Write-Host "SPIFFS files extracted successfully" -ForegroundColor Green

Write-Host ""
Write-Host "Step 4: Cleaning up temporary files..." -ForegroundColor Yellow
Remove-Item $tempBinFile -Force
Write-Host "Temporary files removed" -ForegroundColor Green

Write-Host ""
Write-Host "Extracted files:" -ForegroundColor Cyan
$files = Get-ChildItem -Path $OutputFolder -Recurse -File
foreach ($file in $files) {
    $relativePath = $file.FullName.Substring((Get-Location).Path.Length + 1)
    $size = $file.Length
    Write-Host "  $relativePath ($size bytes)" -ForegroundColor White
}

$totalFiles = $files.Count
$totalSize = ($files | Measure-Object -Property Length -Sum).Sum

Write-Host ""
Write-Host "Summary:" -ForegroundColor Cyan
Write-Host "  Files extracted: $totalFiles" -ForegroundColor Green
Write-Host "  Total size: $totalSize bytes" -ForegroundColor Green

$duration = (Get-Date) - $startTime
Write-Host ""
Write-Host "SPIFFS read completed in $($duration.TotalSeconds) seconds" -ForegroundColor Green
