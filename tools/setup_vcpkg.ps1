Param(
    [string]$VcpkgRoot = "$PSScriptRoot/../vcpkg"
)

$VcpkgRoot = Resolve-Path $VcpkgRoot

if (-not (Test-Path "$VcpkgRoot/.git")) {
    Write-Output "[vcpkg] Cloning into $VcpkgRoot..."
    git clone https://github.com/microsoft/vcpkg.git $VcpkgRoot
}

Write-Output "[vcpkg] Bootstrapping..."
& "$VcpkgRoot/bootstrap-vcpkg.bat"

Write-Output "[vcpkg] Ready at $VcpkgRoot"
