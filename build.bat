@echo off
setlocal EnableDelayedExpansion

echo =============================================
echo  so_drive - qmake Build Script
echo =============================================
echo.

:: -----------------------------------------------
:: 1. Qt yolunu bul (elle de degistirilebilir)
:: -----------------------------------------------
set QT_DIR=
set MINGW_DIR=

:: Bilinen konumlari dene
for %%P in (
    "C:\Qt\6.10.1\mingw_64"
    "D:\Qt\6.10.1\mingw_64"
    "E:\Qt\6.10.1\mingw_64"
    "%USERPROFILE%\Qt\6.10.1\mingw_64"
) do (
    if exist "%%~P\bin\qmake.exe" (
        set QT_DIR=%%~P
        goto :found_qt
    )
)

echo [HATA] Qt 6.10.1 MinGW bulunamadi.
echo.
echo Lutfen asagidaki satiri bu dosyada kendiniz duzenltin:
echo   set QT_DIR=C:\Qt\6.10.1\mingw_64
echo.
echo Qt'yi indirmek icin: https://www.qt.io/download-qt-installer
pause & exit /b 1

:found_qt
echo [OK] Qt bulundu: %QT_DIR%

:: MinGW yolunu bul
for %%P in (
    "C:\Qt\Tools\mingw1310_64"
    "D:\Qt\Tools\mingw1310_64"
    "E:\Qt\Tools\mingw1310_64"
    "%USERPROFILE%\Qt\Tools\mingw1310_64"
) do (
    if exist "%%~P\bin\mingw32-make.exe" (
        set MINGW_DIR=%%~P
        goto :found_mingw
    )
)

echo [HATA] MinGW 13.1.0 araclari bulunamadi.
echo Lutfen Qt Maintenance Tool ile "MinGW 13.1.0 64-bit" bileseni ekleyin.
pause & exit /b 1

:found_mingw
echo [OK] MinGW bulundu: %MINGW_DIR%
echo.

:: -----------------------------------------------
:: 2. Derleme dizini
:: -----------------------------------------------
set BUILD_DIR=%~dp0build\release-cli
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

:: -----------------------------------------------
:: 3. PATH'i guncelle
:: -----------------------------------------------
set PATH=%QT_DIR%\bin;%MINGW_DIR%\bin;%PATH%

:: -----------------------------------------------
:: 4. qmake
:: -----------------------------------------------
echo [1/2] qmake - Makefile olusturuluyor...
cd /d "%BUILD_DIR%"
qmake "%~dp0so_drive.pro" -spec win32-g++ "CONFIG+=release"
if errorlevel 1 (
    echo [HATA] qmake basarisiz.
    pause & exit /b 1
)

:: -----------------------------------------------
:: 5. Derleme
:: -----------------------------------------------
echo.
echo [2/2] Derleniyor ^(-%NUMBER_OF_PROCESSORS% is parcacigi^)...
mingw32-make -j%NUMBER_OF_PROCESSORS%
if errorlevel 1 (
    echo [HATA] Derleme basarisiz.
    pause & exit /b 1
)

:: -----------------------------------------------
:: 6. windeployqt - DLL'leri topla
:: -----------------------------------------------
echo.
echo [+] Qt DLL'leri kopyalaniyor (windeployqt)...
set EXE=%BUILD_DIR%\release\so_drive.exe
if not exist "%EXE%" set EXE=%BUILD_DIR%\so_drive.exe
if exist "%QT_DIR%\bin\windeployqt.exe" (
    windeployqt "%EXE%"
    echo [OK] DLL'ler kopyalandi.
) else (
    echo [UYARI] windeployqt bulunamadi, DLL'ler elle kopyalanmali.
)

echo.
echo =============================================
echo  Derleme TAMAMLANDI
echo  Cikti: %BUILD_DIR%
echo =============================================
pause
