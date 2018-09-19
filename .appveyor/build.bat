call "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvars64.bat"
@echo on

set EXITCODE=0
cmake --version

rem build only ninja with make first
md build-make || goto FAIL
cd build-make || goto FAIL
cmake ^
    -G "NMake Makefiles" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_VERBOSE_MAKEFILE=ON ^
    -DNINJA_BUILD_TESTS=OFF ^
    .. ^
    || goto FAIL
cmake --build . -- ninja || goto FAIL
set NINJA=%CD%/ninja
cd .. || goto FAIL

rem build everything with ninja second
md build-ninja || goto FAIL
cd build-ninja || goto FAIL
cmake ^
    -GNinja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_MAKE_PROGRAM=%NINJA% .. ^
    || goto FAIL
cmake --build . -- -j2 -v || goto FAIL
ctest --output-on-failure || goto FAIL
cd .. || goto FAIL

goto SUCCESS
:FAIL
set EXITCODE=1
:SUCCESS
EXIT /B %EXITCODE%