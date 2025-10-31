# HTML Sync Validation Script for Windows PowerShell
# Checks if config_interface.html and swim_pacer.ino contain identical HTML content

Write-Host "Swim Pacer HTML Sync Checker" -ForegroundColor Cyan
Write-Host "=================================================="

function Extract-HtmlFromStandalone {
    if (-not (Test-Path "config_interface.html")) {
        Write-Host "ERROR: config_interface.html not found" -ForegroundColor Red
        return $null
    }
    
    $content = Get-Content "config_interface.html" -Raw
    
    $startMarker = "<!-- ========== SYNC MARKER: START ESP32 HTML ========== -->"
    $endMarker = "<!-- ========== SYNC MARKER: END ESP32 HTML ========== -->"
    
    $startIdx = $content.IndexOf($startMarker)
    $endIdx = $content.IndexOf($endMarker)
    
    if ($startIdx -eq -1 -or $endIdx -eq -1) {
        Write-Host "ERROR: Sync markers not found in config_interface.html" -ForegroundColor Red
        return $null
    }
    
    # Extract HTML content after start marker, before end marker
    $startIdx = $content.IndexOf("`n", $startIdx) + 1
    $htmlContent = $content.Substring($startIdx, $endIdx - $startIdx).Trim()
    
    return $htmlContent
}

function Extract-HtmlFromIno {
    if (-not (Test-Path "swim_pacer.ino")) {
        Write-Host "ERROR: swim_pacer.ino not found" -ForegroundColor Red
        return $null
    }
    
    $content = Get-Content "swim_pacer.ino" -Raw
    
    # Find the handleRoot function and extract HTML between R"( and )"
    $pattern = 'void handleRoot\(\) \{[\s\S]*?String html = R"\(\s*([\s\S]*?)\s*\)";'
    $match = [regex]::Match($content, $pattern)
    
    if (-not $match.Success) {
        Write-Host "ERROR: Could not find HTML content in swim_pacer.ino handleRoot() function" -ForegroundColor Red
        return $null
    }
    
    $htmlContent = $match.Groups[1].Value.Trim()
    return $htmlContent
}

function Normalize-Html($htmlContent) {
    if (-not $htmlContent) {
        return ""
    }
    
    # Remove comments that aren't sync markers (simplified for PowerShell)
    $htmlContent = $htmlContent -replace '<!--.*?-->', ''
    
    # Normalize whitespace
    $htmlContent = $htmlContent -replace '\s+', ' '
    $htmlContent = $htmlContent -replace '>\s+<', '><'
    
    return $htmlContent.Trim()
}

function Compare-Html {
    Write-Host "Checking HTML synchronization..." -ForegroundColor Yellow
    Write-Host "=================================================="
    
    $standaloneHtml = Extract-HtmlFromStandalone
    $inoHtml = Extract-HtmlFromIno
    
    if ($null -eq $standaloneHtml -or $null -eq $inoHtml) {
        return $false
    }
    
    $normalizedStandalone = Normalize-Html $standaloneHtml
    $normalizedIno = Normalize-Html $inoHtml
    
    if ($normalizedStandalone -eq $normalizedIno) {
        Write-Host "SUCCESS: HTML content is synchronized!" -ForegroundColor Green
        Write-Host "Content length: $($normalizedStandalone.Length) characters" -ForegroundColor Green
        return $true
    } else {
        Write-Host "ERROR: HTML content is NOT synchronized!" -ForegroundColor Red
        Write-Host "Standalone HTML: $($normalizedStandalone.Length) characters" -ForegroundColor Yellow
        Write-Host "ESP32 INO HTML: $($normalizedIno.Length) characters" -ForegroundColor Yellow
        
        # Show first difference (simplified)
        $minLength = [Math]::Min($normalizedStandalone.Length, $normalizedIno.Length)
        for ($i = 0; $i -lt $minLength; $i++) {
            if ($normalizedStandalone[$i] -ne $normalizedIno[$i]) {
                $start = [Math]::Max(0, $i - 50)
                $end = [Math]::Min($normalizedStandalone.Length, $i + 50)
                Write-Host ""
                Write-Host "First difference at position $i" -ForegroundColor Yellow
                Write-Host "Standalone: ...$($normalizedStandalone.Substring($start, $end - $start))..." -ForegroundColor Cyan
                Write-Host "ESP32:      ...$($normalizedIno.Substring($start, [Math]::Min($normalizedIno.Length, $end) - $start))..." -ForegroundColor Magenta
                break
            }
        }
        
        return $false
    }
}

# Main execution
$success = Compare-Html

if ($success) {
    Write-Host ""
    Write-Host "Files are in sync! Ready for development." -ForegroundColor Green
    exit 0
} else {
    Write-Host ""
    Write-Host "Files need synchronization." -ForegroundColor Yellow
    Write-Host "Update the ESP32 code or standalone HTML to match." -ForegroundColor Cyan
    exit 1
}