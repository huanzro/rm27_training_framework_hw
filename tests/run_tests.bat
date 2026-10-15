@echo off
REM Build and run the host-side tests for libs/.
REM Requires a host g++ in PATH (MinGW / MSYS2).
REM Usage: run from anywhere    tests\run_tests.bat

setlocal
cd /d "%~dp0.."

echo [1/2] Compiling tests ...
g++ -std=c++17 -I libs/include libs/src/pid.cpp libs/src/math.cpp tests/test_pid.cpp -o tests/test_pid.exe
if errorlevel 1 goto :fail

echo [2/2] Running tests ...
echo.
tests\test_pid.exe
set RC=%ERRORLEVEL%

echo.
if %RC%==0 echo ===== ALL TESTS PASSED =====
if not %RC%==0 echo ===== SOME TESTS FAILED (exit %RC%) =====
exit /b %RC%

:fail
echo.
echo *** COMPILE FAILED ***
exit /b 1
