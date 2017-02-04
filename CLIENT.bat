@echo off

set MSDEV=BuildConsole
set CONFIG=/ShowTime /ShowAgent /nologo /cfg=
set MSDEV=msdev
set CONFIG=/make 
set build_type=release
set BUILD_ERROR=
call vcvars32

%MSDEV% cl_dll/cl_dll.dsp %CONFIG%"cl_dll - Win32 Release" %build_target%
if errorlevel 1 set BUILD_ERROR=1

%MSDEV% dlls/hl.dsp %CONFIG%"hl - Win32 Release" %build_target%
if errorlevel 1 set BUILD_ERROR=1

if "%BUILD_ERROR%"=="" goto build_ok

echo *********************
echo *********************
echo *** Build Errors! ***
echo *********************
echo *********************
echo press any key to exit
echo *********************
pause>nul
goto done


@rem
@rem Successful build
@rem
:build_ok

rem //delete log files
if exist cl_dll\cl_dll.plg del /f /q cl_dlls\cl_dlls.plg
if exist dlls\hl.plg del /f /q dlls\hl.plg

echo
echo 	     Build succeeded!
echo
:done
pause