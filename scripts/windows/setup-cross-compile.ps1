# Setup cross-compilation on Windows

param(
    [string]$Target
)

Write-Host "Setting up cross-compilation for $Target..."

switch ($Target) {
    "linux" {
        # Setup WSL for Linux compilation
        if (Get-Command wsl -ErrorAction SilentlyContinue) {
            Write-Host "Setting up Linux toolchain in WSL..."
            wsl sudo apt-get update
            wsl sudo apt-get install -y build-essential
        } else {
            Write-Host "Install WSL2 for Linux cross-compilation:"
            Write-Host "  wsl --install -d Ubuntu"
        }
    }
    
    "darwin" {
        Write-Host "macOS cross-compilation from Windows requires Docker or VM."
        Write-Host "Consider using a macOS machine or CI service."
    }
    
    "bsd" {
        Write-Host "BSD cross-compilation from Windows requires Docker or VM."
        Write-Host "Consider using a BSD machine or CI service."
    }
    
    default {
        Write-Host "Unsupported target: $Target"
    }
}