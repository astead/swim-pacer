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
    $endTime = Get-Date
    $uploadDuration = $endTime - $startTime - $compileDuration
    Write-Host "Upload time: $($uploadDuration.Hours) hours, $($uploadDuration.Minutes) minutes, $($uploadDuration.Seconds) seconds" -ForegroundColor Green
} else {
    Write-Host "Upload failed - use Arduino IDE instead" -ForegroundColor Yellow
    exit 1
}

Write-Host ""
Write-Host "=== STEP 2: SPIFFS ===" -ForegroundColor Yellow

$spiffsDataPath = "data"
$spiffsFiles = Get-ChildItem -Path $spiffsDataPath -Recurse | Where-Object { -not $_.PSIsContainer }
if ($spiffsFiles.Count -eq 0) {
    Write-Host "No files found in data folder. Skipping SPIFFS deployment." -ForegroundColor Yellow
    Write-Host ""
} else {
    Write-Host "Files found in data folder. Proceeding with SPIFFS deployment." -ForegroundColor Green
    # Check last modified date of files in data folder to see if it is newer than last SPIFFS binary
    $lastSpiffsBin = Get-ChildItem -Path ./build/spiffs_deploy.bin -ErrorAction SilentlyContinue
    if ($lastSpiffsBin) {
        $lastSpiffsDate = $lastSpiffsBin.LastWriteTime
        $newerFiles = $spiffsFiles | Where-Object { $_.LastWriteTime -gt $lastSpiffsDate }
        if ($newerFiles.Count -eq 0) {
            Write-Host "No new files found. Skipping SPIFFS deployment." -ForegroundColor Yellow
            Write-Host ""
        } else {
            Write-Host "New or modified files found. Proceeding with SPIFFS deployment." -ForegroundColor Green
            # Call deploy-spiffs.ps1 to handle SPIFFS deployment
            .\deploy-spiffs.ps1 -Port $Port
            if ($LASTEXITCODE -ne 0) {
                Write-Host "SPIFFS deployment failed." -ForegroundColor Red
                exit 1
            }
        }
    } else {
        Write-Host "No existing SPIFFS binary found. Proceeding with SPIFFS deployment." -ForegroundColor Green
        # Call deploy-spiffs.ps1 to handle SPIFFS deployment
        .\deploy-spiffs.ps1 -Port $Port
        if ($LASTEXITCODE -ne 0) {
            Write-Host "SPIFFS deployment failed." -ForegroundColor Red
            exit 1
        }
    }
}

Write-Host ""
Write-Host "=== DEPLOYMENT COMPLETE ===" -ForegroundColor Cyan
# Calculate and display duration in human readable format
$endTime = Get-Date
$totalDuration = $endTime - $startTime
Write-Host "Total deployment time: $($totalDuration.Hours) hours, $($totalDuration.Minutes) minutes, $($totalDuration.Seconds) seconds" -ForegroundColor Green
Write-Host ""