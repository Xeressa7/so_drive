@echo off
setlocal

echo =============================================
echo  so_drive - Qt Kurulum Scripti
echo  QtCreator GEREKTIRMEZ
echo =============================================
echo.

:: Python kontrolu
python --version >nul 2>&1
if errorlevel 1 (
    echo [HATA] Python bulunamadi.
    echo Python'u https://python.org adresinden indirin.
    pause & exit /b 1
)
echo [OK] Python bulundu.

:: pip ile aqtinstall kur
echo.
echo [1/3] aqtinstall kuruluyor...
pip install aqtinstall --quiet
if errorlevel 1 (
    echo [HATA] aqtinstall kurulamadi.
    pause & exit /b 1
)
echo [OK] aqtinstall hazir.

:: Qt 6.10.1 MinGW kutuphanelerini indir (IDE yok, sadece kutuphaneler)
echo.
echo [2/3] Qt 6.10.1 MinGW 64-bit indiriliyor...
echo (Boyut: ~600MB, suresi internet hiziniza gore degisir)
aqt install-qt windows desktop 6.10.1 win64_mingw -O C:\Qt
if errorlevel 1 (
    echo [HATA] Qt indirilemedi.
    pause & exit /b 1
)
echo [OK] Qt kuruldu: C:\Qt\6.10.1\mingw_64

:: MinGW derleyicisini indir
echo.
echo [3/3] MinGW 13.1.0 derleyicisi indiriliyor...
aqt install-tool windows desktop tools_mingw1310 -O C:\Qt
if errorlevel 1 (
    echo [HATA] MinGW indirilemedi.
    pause & exit /b 1
)
echo [OK] MinGW kuruldu: C:\Qt\Tools\mingw1310_64

echo.
echo =============================================
echo  Kurulum TAMAMLANDI
echo
echo  Simdiden yapabileceklerin:
echo   - Kodu VS Code / Cursor ile duzenle
echo   - UI dosyalarini: C:\Qt\6.10.1\mingw_64\bin\designer.exe
echo   - Derlemek icin: build.bat calistir
echo =============================================
pause
