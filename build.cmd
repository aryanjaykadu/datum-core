@echo off
rem Configure and build with the pip-installed zig toolchain (no Visual Studio needed).
rem   pip install ziglang cmake ninja
setlocal
set "PATH=%~dp0tools;%APPDATA%\..\Local\Python\pythoncore-3.14-64\Scripts;%PATH%"
cmake -S "%~dp0." -B "%~dp0build" -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_CXX_COMPILER="%~dp0tools/zig-cxx.cmd" -DCMAKE_AR="%~dp0tools/zig-ar.cmd" -DCMAKE_RANLIB="%~dp0tools/zig-ranlib.cmd" || exit /b 1
cmake --build "%~dp0build" || exit /b 1
