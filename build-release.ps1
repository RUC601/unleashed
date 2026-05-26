$ErrorActionPreference = 'Stop'

$cmake = Join-Path $env:ProgramFiles 'Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) {
    throw "Visual Studio Community 2026 CMake was not found at: $cmake"
}

Push-Location $PSScriptRoot
try {
    & $cmake --preset vs2026
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    & $cmake --build --preset release
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
finally {
    Pop-Location
}
