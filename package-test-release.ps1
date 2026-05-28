$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$releaseDir = Join-Path $root 'build\Release'
$distDir = Join-Path $root 'dist'
$packageName = 'Unleashed-test'
$stageDir = Join-Path $distDir $packageName
$zipPath = Join-Path $distDir ($packageName + '.zip')

$forbiddenPackagePatterns = @(
    '(^|\\)(src|include|vendor|\.git|build)(\\|$)',
    '\.(pdb|ilk|obj|tlog|vcxproj|slnx|lib|h|hpp|cpp|cxx|cc|rc)$',
    '(^|\\)unleashed_.*\.log$',
    '(^|\\)(Scanner|DmaVerify|DecryptDump|DecryptEmu|FinalProbe|HeroProbe|PerkProbe|KeyScan|HostMouseHold|AngleMathTest|CNServerScanner)\.exe$',
    '(^|\\)unicorn\.dll$'
)

function Require-File {
    param(
        [Parameter(Mandatory = $true)][string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required file is missing: $Path"
    }
}

function Require-Directory {
    param(
        [Parameter(Mandatory = $true)][string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "Required directory is missing: $Path"
    }
}

function Assert-CleanPackage {
    param(
        [Parameter(Mandatory = $true)][string]$Path
    )

    $rootFullPath = [System.IO.Path]::GetFullPath($Path).TrimEnd('\') + '\'
    $blockedFiles = New-Object System.Collections.Generic.List[string]

    Get-ChildItem -LiteralPath $Path -Recurse -Force -File | ForEach-Object {
        $fullPath = [System.IO.Path]::GetFullPath($_.FullName)
        if (-not $fullPath.StartsWith($rootFullPath, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Unexpected file outside package folder: $fullPath"
        }

        $relativePath = $fullPath.Substring($rootFullPath.Length)
        foreach ($pattern in $forbiddenPackagePatterns) {
            if ($relativePath -match $pattern) {
                $blockedFiles.Add($relativePath)
                break
            }
        }
    }

    if ($blockedFiles.Count -gt 0) {
        $message = "Refusing to create package because blocked files were staged:`n" +
            ($blockedFiles | Sort-Object | ForEach-Object { "  - $_" }) -join "`n"
        throw $message
    }
}

Push-Location $root
try {
    Write-Host '[1/4] Building Release...'
    & (Join-Path $root 'build-release.ps1')
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    Require-Directory $releaseDir

    $requiredFiles = @(
        'Unleashed.exe',
        'leechcore.dll',
        'vmm.dll',
        'FTD3XX.dll',
        'FTD3XXWU.dll',
        'Helper64.dll',
        'leechcore_driver.dll',
        'leechcore_device_rawtcp.dll',
        'leechcore_device_hvsavedstate.dll'
    )

    foreach ($file in $requiredFiles) {
        Require-File (Join-Path $releaseDir $file)
    }

    Require-Directory (Join-Path $releaseDir 'assets')
    Require-Directory (Join-Path $releaseDir 'config')

    Write-Host '[2/4] Preparing clean package folder...'
    New-Item -ItemType Directory -Force -Path $distDir | Out-Null

    $resolvedDist = [System.IO.Path]::GetFullPath($distDir)
    $resolvedStage = [System.IO.Path]::GetFullPath($stageDir)
    if (-not $resolvedStage.StartsWith($resolvedDist, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean a folder outside dist: $stageDir"
    }

    if (Test-Path -LiteralPath $stageDir) {
        Remove-Item -LiteralPath $stageDir -Recurse -Force
    }
    if (Test-Path -LiteralPath $zipPath) {
        Remove-Item -LiteralPath $zipPath -Force
    }
    New-Item -ItemType Directory -Force -Path $stageDir | Out-Null

    Write-Host '[3/4] Copying runtime files...'
    foreach ($file in $requiredFiles) {
        Copy-Item -LiteralPath (Join-Path $releaseDir $file) -Destination $stageDir -Force
    }

    $configIni = Join-Path $releaseDir 'config.ini'
    if (Test-Path -LiteralPath $configIni -PathType Leaf) {
        Copy-Item -LiteralPath $configIni -Destination $stageDir -Force
    } else {
        Write-Warning 'config.ini was not found in build\Release, so it was not included.'
    }

    Copy-Item -LiteralPath (Join-Path $releaseDir 'assets') -Destination $stageDir -Recurse -Force
    Copy-Item -LiteralPath (Join-Path $releaseDir 'config') -Destination $stageDir -Recurse -Force

    @'
Unleashed test package

Run Unleashed.exe from this folder. Keep the DLL files, assets folder, and config folder next to the EXE.
config.ini is included from the local Release build.

If Windows reports missing VCRUNTIME or MSVCP DLLs, install the Microsoft Visual C++ Redistributable for x64.
'@ | Set-Content -LiteralPath (Join-Path $stageDir 'README.txt') -Encoding ASCII

    Assert-CleanPackage $stageDir

    Write-Host '[4/4] Creating zip...'
    Compress-Archive -LiteralPath $stageDir -DestinationPath $zipPath -CompressionLevel Optimal
    $zipHash = Get-FileHash -LiteralPath $zipPath -Algorithm SHA256

    Write-Host ''
    Write-Host "Done: $zipPath"
    Write-Host "SHA256: $($zipHash.Hash)"
    Write-Host "Staging folder: $stageDir"
}
finally {
    Pop-Location
}
