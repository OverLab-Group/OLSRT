# Check dependencies for Windows

param(
    [string]$Platform = "native",
    [string]$Arch = "x86_64"
)

Write-Host "Checking dependencies for $Platform/$Arch..."

# Check if running as admin
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole] "Administrator")

# Check for Visual Studio
if (Test-Path "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe") {
    Write-Host "✅ Visual Studio: Found"
} else {
    Write-Host "❌ Visual Studio: Not found (optional for native Windows builds)"
}

# Check for MinGW
$mingwPaths = @(
    "${env:ProgramFiles}\mingw-w64\mingw64\bin",
    "${env:ProgramFiles(x86)}\mingw-w64\mingw64\bin"
)

$mingwFound = $false
foreach ($path in $mingwPaths) {
    if (Test-Path $path) {
        Write-Host "✅ MinGW-w64: Found at $path"
        $mingwFound = $true
        break
    }
}

if (-not $mingwFound) {
    Write-Host "❌ MinGW-w64: Not found (required for cross-compilation)"
}

# Check for CMake
if (Get-Command cmake -ErrorAction SilentlyContinue) {
    Write-Host "✅ CMake: $(cmake --version | Select-Object -First 1)"
} else {
    Write-Host "❌ CMake: Not found"
}

# Check for Git
if (Get-Command git -ErrorAction SilentlyContinue) {
    Write-Host "✅ Git: $(git --version)"
} else {
    Write-Host "❌ Git: Not found"
}

# Check for Python
if (Get-Command python -ErrorAction SilentlyContinue) {
    Write-Host "✅ Python: $(python --version)"
} elseif (Get-Command python3 -ErrorAction SilentlyContinue) {
    Write-Host "✅ Python3: $(python3 --version)"
} else {
    Write-Host "⚠ Python: Not found (optional for build scripts)"
}

# Platform-specific checks
if ($Platform -eq "linux" -or $Platform -eq "darwin" -or $Platform -eq "bsd") {
    Write-Host "`nFor $Platform cross-compilation from Windows:" -ForegroundColor Yellow
    Write-Host "1. Install WSL2: https://docs.microsoft.com/en-us/windows/wsl/install"
    Write-Host "2. Or use Docker with the target platform"
}

Write-Host "`nDependency check complete!" -ForegroundColor Green