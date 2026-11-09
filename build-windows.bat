@echo off
REM Run from "x64 Native Tools Command Prompt for VS 2022" (or any shell with VS 2022 + CMake installed)
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCOPY_PLUGIN_AFTER_BUILD=OFF || exit /b 1
cmake --build build --config Release --parallel || exit /b 1
echo.
echo Built: build\BassDriver_artefacts\Release\VST3\Bass Driver.vst3
echo Copy that whole folder to C:\Program Files\Common Files\VST3\
