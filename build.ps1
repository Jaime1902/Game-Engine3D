param(
    [ValidateSet("configure", "build", "test", "run-editor", "run-player", "vulkan", "all")]
    [string]$Action = "all"
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
$buildDir = Join-Path $root "out\build\windows-dev"
$vulkanBuildDir = Join-Path $root "out\build\windows-vulkan"

if (-not $env:VULKAN_SDK -and (Test-Path -LiteralPath "C:\VulkanSDK\1.4.363.1")) {
    $env:VULKAN_SDK = "C:\VulkanSDK\1.4.363.1"
}
if (-not (Test-Path -LiteralPath $cmake -PathType Leaf)) {
    throw "CMake no encontrado: $cmake"
}

function Invoke-CMakeStep {
    param([string[]]$Arguments, [string]$Description)
    & $cmake @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Description falló con código $LASTEXITCODE" }
}

Push-Location $root
try {
    switch ($Action) {
        "configure" { Invoke-CMakeStep @("--preset", "windows-dev") "Configuración" }
        "build" { Invoke-CMakeStep @("--build", "--preset", "windows-dev") "Compilación" }
        "test" {
            if (-not (Test-Path -LiteralPath $ctest -PathType Leaf)) { throw "CTest no encontrado: $ctest" }
            & $ctest --preset windows-dev
            if ($LASTEXITCODE -ne 0) { throw "CTest falló con código $LASTEXITCODE" }
        }
        "run-editor" {
            & (Join-Path $buildDir "apps\editor\Debug\ge3d_editor.exe")
            if ($LASTEXITCODE -ne 0) { throw "Editor finalizó con código $LASTEXITCODE" }
        }
        "run-player" {
            & (Join-Path $buildDir "apps\player\Debug\ge3d_player.exe")
            if ($LASTEXITCODE -ne 0) { throw "Player finalizó con código $LASTEXITCODE" }
        }
        "vulkan" {
            Invoke-CMakeStep @("--preset", "windows-vulkan") "Configuración Vulkan"
            Invoke-CMakeStep @("--build", "--preset", "windows-vulkan") "Compilación Vulkan"
            & (Join-Path $vulkanBuildDir "tools\Debug\ge3d_vulkan_probe.exe")
            if ($LASTEXITCODE -ne 0) { throw "El sondeo Vulkan falló con código $LASTEXITCODE" }
        }
        "all" {
            Invoke-CMakeStep @("--preset", "windows-dev") "Configuración"
            Invoke-CMakeStep @("--build", "--preset", "windows-dev") "Compilación"
            & $ctest --preset windows-dev
            if ($LASTEXITCODE -ne 0) { throw "CTest falló con código $LASTEXITCODE" }
            & (Join-Path $buildDir "apps\editor\Debug\ge3d_editor.exe")
            if ($LASTEXITCODE -ne 0) { throw "Editor finalizó con código $LASTEXITCODE" }
        }
    }
}
finally {
    Pop-Location
}