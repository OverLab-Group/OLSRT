# Install dependencies on Windows

param(
    [string]$Target = "all"  # all, windows, linux, darwin, bsd
)

Write-Host "Installing dependencies for $Target..." -ForegroundColor Cyan

# Check if running as admin
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole] "Administrator")
if (-not $isAdmin) {
    Write-Host "Please run as Administrator!" -ForegroundColor Red
    exit 1
}

# Check for Chocolatey (Windows package manager)
if (-not (Get-Command choco -ErrorAction SilentlyContinue)) {
    Write-Host "Installing Chocolatey..." -ForegroundColor Yellow
    Set-ExecutionPolicy Bypass -Scope Process -Force
    [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
    iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))
}

# Base packages for all targets
$packages = @(
    "git",
    "cmake",
    "python",
    "visualstudio2022-buildtools"  # MSVC compiler
)

# Target-specific packages
switch ($Target) {
    "windows" {
        $packages += "mingw"
        $packages += "make"
    }
    "linux" {
        $packages += "wsl2"
    }
    "all" {
        $packages += "mingw"
        $packages += "make"
        $packages += "wsl2"
    }
}

# Install packages
foreach ($package in $packages) {
    Write-Host "Installing $package..." -ForegroundColor Yellow
    choco install $package -y --no-progress
}

# For Linux cross-compilation, setup WSL
if ($Target -eq "linux" -or $Target -eq "all") {
    Write-Host "`nSetting up WSL for Linux compilation..." -ForegroundColor Cyan
    wsl --install -d Ubuntu
}

Write-Host "`nInstallation complete!" -ForegroundColor Green
Write-Host "You may need to restart your terminal for changes to take effect." -ForegroundColor Yellow