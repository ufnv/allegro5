@echo off
rem build_selftest.cmd <allegro source dir> <allegro build dir>
set SRC=%~1
set BLD=%~2
rem third-party libraries: the package's deps folder
for %%I in ("%~dp0..\deps") do set DEPS=%%~fI
set L=%BLD%\lib\RelWithDebInfo
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cl /nologo /MT /O2 /EHsc /DALLEGRO_STATICLINK /I"C:\dxsdk\Include" /I"%SRC%\include" /I"%BLD%\include" ^
  /I"%SRC%\addons\font" /I"%SRC%\addons\ttf" /I"%SRC%\addons\image" /I"%SRC%\addons\primitives" ^
  icbm_selftest.cpp /link /out:icbm_selftest.exe ^
  "%L%\allegro_ttf-static.lib" "%L%\allegro_font-static.lib" "%L%\allegro_primitives-static.lib" ^
  "%L%\allegro_image-static.lib" "%L%\allegro-static.lib" ^
  "%DEPS%\lib\freetype.lib" "%DEPS%\lib\libpng16.lib" "%DEPS%\lib\zlib.lib" "%DEPS%\lib\jpeg.lib" "%DEPS%\lib\webp.lib" ^
  user32.lib gdi32.lib comdlg32.lib ole32.lib winmm.lib kernel32.lib psapi.lib shlwapi.lib ^
  C:\dxsdk\Lib\x64\d3d9.lib C:\dxsdk\Lib\x64\dinput8.lib opengl32.lib glu32.lib shell32.lib oleaut32.lib uuid.lib advapi32.lib
