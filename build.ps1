# Script de conveniencia para Game-Engine3D
$ErrorActionPreference = "Stop"
$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"

param(
    [string]$Action = "build"
)

switch ($Action) {
    "configure" {
        & $cmake --preset windows-dev
    }
    "build" {
        & $cmake --build --preset windows-dev
    }
    "test" {
        & $ctest --preset windows-dev
    }
    "run-editor" {
        & .\out\build\windows-dev\apps\editor\Debug\ge3d_editor.exe
    }
    "run-player" {
        & .\out\build\windows-dev\apps\player\Debug\ge3d_player.exe
    }
    "all" {
        & $cmake --preset windows-dev
        & $cmake --build --preset windows-dev
        & $ctest --preset windows-dev
        & .\out\build\windows-dev\apps\editor\Debug\ge3d_editor.exe
    }
    Default {
        Write-Host "Uso: .\build.ps1 [configure|build|test|run-editor|run-player|all]"
    }
}
