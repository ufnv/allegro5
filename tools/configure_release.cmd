@echo off
rem Configure the static Allegro libraries ICBM links (VS 2026, x64, /MT,
rem RelWithDebInfo) - same options as the previous hand-made build.
rem   configure_release.cmd <allegro source dir>    -> builds into <src>\build
set SRC=%~1
set BLD=%~1\build
rem third-party libraries: the package's deps folder
for %%I in ("%~dp0..\deps") do set DEPS=%%~fI
rem the ICBM_STAGE target (build + copy into the game's allegro5 folder)
set STAGE=%~dp0icbm_stage.cmake
set STAGE=%STAGE:\=/%
set CMAKE="C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
%CMAKE% -S "%SRC%" -B "%BLD%" -G "Visual Studio 18 2026" -A x64 ^
  -DSHARED=OFF -DWANT_STATIC_RUNTIME=ON -DPREFER_STATIC_DEPS=ON ^
  -DWANT_DOCS=OFF -DWANT_DEMO=OFF -DWANT_EXAMPLES=OFF -DWANT_TESTS=OFF ^
  -DWANT_D3D=ON -DWANT_OPENGL=ON -DWANT_SHADERS_D3D=ON -DWANT_SHADERS_GL=ON ^
  -DWANT_VIDEO=ON -DWANT_TTF=ON -DWANT_PHYSFS=ON -DWANT_NATIVE_DIALOG=ON -DWANT_RELEASE_LOGGING=ON ^
  -DCMAKE_PREFIX_PATH="%DEPS%" -DCMAKE_INCLUDE_PATH="%DEPS%\include" -DCMAKE_LIBRARY_PATH="%DEPS%\lib" ^
  -DD3DX9_INCLUDE_DIR=C:/dxsdk/Include -DD3DX9_LIBRARY=C:/dxsdk/Lib/x64/d3dx9.lib ^
  -DFLAC_STATIC=ON -DFLAC_INCLUDE_DIR="%DEPS%\include" -DFLAC_LIBRARY="%DEPS%\lib\FLAC.lib" ^
  -DFREETYPE_BZIP2=OFF -DFREETYPE_PNG=ON -DFREETYPE_ZLIB=ON -DFREETYPE_HARFBUZZ=OFF ^
  -DCMAKE_PROJECT_INCLUDE="%STAGE%"
