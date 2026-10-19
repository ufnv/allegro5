@echo off
rem Configure the latest allegro (x64, static, like the ICBM build) into %2
set SRC=%~1
set BLD=%~2
rem third-party libraries: the package's deps folder
for %%I in ("%~dp0..\deps") do set DEPS=%%~fI
set CMAKE="C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
%CMAKE% -S "%SRC%" -B "%BLD%" -G "Visual Studio 18 2026" -A x64 ^
  -DSHARED=OFF -DWANT_STATIC_RUNTIME=ON -DWANT_DOCS=OFF -DWANT_DEMO=OFF ^
  -DWANT_EXAMPLES=ON -DWANT_TESTS=ON -DWANT_VIDEO=ON -DWANT_D3D=ON -DWANT_OPENGL=ON ^
  -DCMAKE_PREFIX_PATH="%DEPS%" -DCMAKE_INCLUDE_PATH="%DEPS%\include" -DCMAKE_LIBRARY_PATH="%DEPS%\lib" ^
  -DD3DX9_INCLUDE_DIR=C:/dxsdk/Include -DD3DX9_LIBRARY=C:/dxsdk/Lib/x64/d3dx9.lib ^
  -DFREETYPE_BZIP2=OFF -DFREETYPE_PNG=ON -DFREETYPE_ZLIB=ON -DFREETYPE_HARFBUZZ=OFF
