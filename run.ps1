# PowerShell launch script for NightWatch
Set-Location $PSScriptRoot
cmd.exe /c "cd /d `"$PSScriptRoot`" && call `"$PSScriptRoot\build.bat`""
if ($LASTEXITCODE -eq 0 -and (Test-Path "build-mingw\NightWatch.exe")) {
    Write-Host "[INFO] Launching NightWatch..." -ForegroundColor Green
    Start-Process -FilePath ".\NightWatch.exe" -WorkingDirectory "$PSScriptRoot\build-mingw"
} else {
    Write-Host "[ERROR] Build failed." -ForegroundColor Red
}
