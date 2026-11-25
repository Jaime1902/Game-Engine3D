@echo off
rem ===========================================================================
rem  Game-Engine3D - compilar, probar y ejecutar
rem
rem  Uso:  build [accion] [msvc]
rem
rem  Acciones:
rem    editor       compila el editor grafico Vulkan y lo abre   (por defecto)
rem    all          compila la base, ejecuta las pruebas y el editor de consola
rem    configure    solo configura CMake
rem    build        solo compila la base
rem    test         ejecuta las pruebas
rem    run-player   ejecuta el player de consola
rem    vulkan       compila el editor Vulkan y ejecuta el diagnostico
rem    clean        borra la carpeta out\build
rem    help         muestra esta ayuda
rem
rem  Por defecto usa MinGW (g++ y Ninja). Con "msvc" como segundo
rem  argumento usa Visual Studio 2022.
rem ===========================================================================
setlocal EnableExtensions

set "ROOT=%~dp0"
set "ACTION=%~1"
if "%ACTION%"=="" set "ACTION=editor"
set "TOOLCHAIN=%~2"
if "%TOOLCHAIN%"=="" set "TOOLCHAIN=mingw"

rem Si se abrio con doble clic y algo falla, se pausa al final para poder leer el error.
rem (Con doble clic no hay argumentos y cmd se lanza con /c apuntando a este archivo.)
set "PAUSE_AT_END="
if "%~1"=="" echo %CMDCMDLINE% | find /i "%~nx0" >nul && set "PAUSE_AT_END=1"

if /i "%TOOLCHAIN%"=="mingw" (
    set "PRESET=windows-mingw"
    set "VULKAN_PRESET=windows-mingw-vulkan"
    set "CONFIG_DIR="
) else if /i "%TOOLCHAIN%"=="msvc" (
    set "PRESET=windows-dev"
    set "VULKAN_PRESET=windows-vulkan"
    set "CONFIG_DIR=\Debug"
) else (
    echo Compilador desconocido: %TOOLCHAIN%. Usa mingw o msvc.
    set "ERR=2"
    goto :end
)
set "BUILD_DIR=%ROOT%out\build\%PRESET%"
set "VULKAN_DIR=%ROOT%out\build\%VULKAN_PRESET%"

rem CMake: primero el del PATH, si no el que trae Visual Studio 2022 Build Tools.
where cmake >nul 2>nul
if errorlevel 1 (
    set "VS_CMAKE=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
    if exist "%VS_CMAKE%\cmake.exe" (
        set "PATH=%VS_CMAKE%;%PATH%"
    ) else (
        echo No se encontro CMake. Instalalo o agregalo al PATH.
        set "ERR=1"
        goto :end
    )
)

rem Vulkan SDK: si la variable no esta definida, se toma la ultima version de C:\VulkanSDK.
if not defined VULKAN_SDK (
    for /d %%D in ("C:\VulkanSDK\*") do set "VULKAN_SDK=%%~fD"
)

set "ERR=0"
pushd "%ROOT%"
for %%A in (editor all configure build test run-player vulkan clean help) do (
    if /i "%ACTION%"=="%%A" goto :act_%%A
)
echo Accion desconocida: %ACTION%
echo.
set "ERR=2"
goto :act_help

:act_help
echo Uso: build [editor^|all^|configure^|build^|test^|run-player^|vulkan^|clean^|help] [msvc]
echo   editor      compila el editor grafico Vulkan y lo abre (por defecto)
echo   all         compila la base, ejecuta las pruebas y el editor de consola
echo   configure   solo configura CMake
echo   build       solo compila la base
echo   test        ejecuta las pruebas
echo   run-player  ejecuta el player de consola
echo   vulkan      compila el editor Vulkan y ejecuta el diagnostico
echo   clean       borra la carpeta out\build
goto :done

:act_configure
call :step "Configuracion" cmake --preset %PRESET% || goto :done
goto :done

:act_build
call :step "Configuracion" cmake --preset %PRESET% || goto :done
call :step "Compilacion" cmake --build --preset %PRESET% || goto :done
goto :done

:act_test
call :step "Pruebas" ctest --preset %PRESET% || goto :done
goto :done

:act_run-player
call :step "Player" "%BUILD_DIR%\apps\player%CONFIG_DIR%\ge3d_player.exe" || goto :done
goto :done

:act_all
call :step "Configuracion" cmake --preset %PRESET% || goto :done
call :step "Compilacion" cmake --build --preset %PRESET% || goto :done
call :step "Pruebas" ctest --preset %PRESET% || goto :done
call :step "Editor de consola" "%BUILD_DIR%\apps\editor%CONFIG_DIR%\ge3d_editor.exe" || goto :done
goto :done

:act_vulkan
call :need_vulkan || goto :done
call :step "Configuracion Vulkan" cmake --preset %VULKAN_PRESET% || goto :done
call :step "Compilacion Vulkan" cmake --build --preset %VULKAN_PRESET% || goto :done
call :step "Diagnostico Vulkan" "%VULKAN_DIR%\tools%CONFIG_DIR%\ge3d_vulkan_probe.exe" || goto :done
goto :done

:act_editor
call :need_vulkan || goto :done
call :step "Configuracion Vulkan" cmake --preset %VULKAN_PRESET% || goto :done
call :step "Compilacion Vulkan" cmake --build --preset %VULKAN_PRESET% || goto :done
echo.
echo Abriendo el editor... (los mensajes aparecen aqui y en la Consola del editor)
call :step "Editor" "%VULKAN_DIR%\apps\vulkan-editor%CONFIG_DIR%\ge3d_vulkan_editor.exe" || goto :done
goto :done

:act_clean
if exist "%ROOT%out\build" (
    echo Borrando %ROOT%out\build ...
    rmdir /s /q "%ROOT%out\build"
)
goto :done

rem ---------------------------------------------------------------------------
rem  :step "Descripcion" comando args...   Ejecuta y corta si falla.
:step
set "STEP_NAME=%~1"
shift
set "STEP_CMD="
:step_args
if "%~1"=="" goto :step_run
set STEP_CMD=%STEP_CMD% "%~1"
shift
goto :step_args
:step_run
echo.
echo === %STEP_NAME% ===
call %STEP_CMD%
if errorlevel 1 (
    set "ERR=%errorlevel%"
    echo.
    echo *** %STEP_NAME% fallo con codigo %errorlevel% ***
    exit /b 1
)
exit /b 0

:need_vulkan
if not defined VULKAN_SDK (
    echo No se encontro el Vulkan SDK. Instalalo desde https://vulkan.lunarg.com
    echo y abre una terminal nueva.
    set "ERR=1"
    exit /b 1
)
exit /b 0

:done
popd

:end
if defined PAUSE_AT_END if not "%ERR%"=="0" (
    echo.
    pause
)
endlocal & exit /b %ERR%
