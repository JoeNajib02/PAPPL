Param(
    [Parameter(Mandatory = $true)]
    [string]$MetadataJson,

    [Parameter(Mandatory = $true)]
    [string]$TargetsCsv,

    [Parameter(Mandatory = $true)]
    [string]$RefPcap,

    [Parameter(Mandatory = $true)]
    [string[]]$Pcaps,

    [string]$OutputDir = "artifacts\rail_repeatability",
    [double]$GridResM = 0.05,
    [int]$MaxScans = 0,
    [switch]$NoAppend
)

# Guard rails: require the executable.
$exe = ".\build-win\Release\compare_point_repeatability.exe"
if (-not (Test-Path $exe)) {
    Write-Error "Missing executable: $exe. Build it first."
    exit 1
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$appendMode = -not $NoAppend

foreach ($pcap in $Pcaps) {
    if (-not (Test-Path $pcap)) {
        Write-Warning "Skipping missing PCAP: $pcap"
        continue
    }

    $label = [IO.Path]::GetFileNameWithoutExtension($pcap)
    $outCsv = Join-Path $OutputDir ("rail_results_{0}.csv" -f $label)
    $tmp = New-TemporaryFile

    Write-Host "Processing $pcap -> $outCsv (grid=$GridResM, max_scans=$MaxScans, append=$appendMode)"

    & $exe `
        $MetadataJson `
        $TargetsCsv `
        $tmp `
        $RefPcap `
        $pcap `
        ("--grid-res-m={0}" -f $GridResM) `
        ("--max-scans={0}" -f $MaxScans)

    if ($LASTEXITCODE -ne 0) {
        Write-Warning "compare_point_repeatability failed for $pcap (exit $LASTEXITCODE). Keeping temp at $tmp"
        continue
    }

    if ($appendMode -and (Test-Path $outCsv)) {
        # Skip header when appending.
        Get-Content $tmp | Select-Object -Skip 1 | Add-Content $outCsv
        Remove-Item $tmp -Force
    } else {
        Move-Item $tmp $outCsv -Force
    }
}
