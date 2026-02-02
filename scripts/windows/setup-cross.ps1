# Setup cross-compilation on Windows

param(
    [string]$TargetPlatform = "linux",
    [string]$TargetArch = "x86_64"
)

Write-Host "Setting up cross-compilation to $TargetPlatform/$TargetArch..." -ForegroundColor Cyan

switch ($TargetPlatform) {
    "linux" {
        Write-Host "Setting up Linux cross-compilation via WSL..." -ForegroundColor Yellow
        
        # Check if WSL is available
        if (Get-Command wsl -ErrorAction SilentlyContinue) {
            Write-Host "WSL is available. Setting up Linux toolchain..."
            
            # Install Linux build tools in WSL
            wsl sudo apt-get update
            wsl sudo apt-get install -y gcc g++ make cmake
            
            Write-Host "Linux toolchain setup complete in WSL!" -ForegroundColor Green
        } else {
            Write-Host "WSL not available. Please install WSL2 first." -ForegroundColor Red
            Write-Host "Run: wsl --install" -ForegroundColor Yellow
        }
    }
    
    "darwin" {
        Write-Host "Setting up macOS cross-compilation..." -ForegroundColor Yellow
        Write-Host "Note: macOS cross-compilation from Windows requires osxcross." -ForegroundColor Yellow
        Write-Host "Consider using a macOS machine or CI service." -ForegroundColor Yellow
    }
    
    "bsd" {
        Write-Host "Setting up BSD cross-compilation..." -ForegroundColor Yellow
        Write-Host "Note: Use WSL with FreeBSD or native BSD tools." -ForegroundColor Yellow
    }
    
    default {
        Write-Host "Target platform $TargetPlatform not supported for cross-compilation from Windows." -ForegroundColor Red
    }
}

Write-Host "`nCross-compilation setup complete!" -ForegroundColor Green