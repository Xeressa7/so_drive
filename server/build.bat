@echo off
:: ─── so_drive Sunucu Derleyici (Windows / MinGW) ─────────────────────────────
::
:: Gereksinimler:
::   - g++ (MinGW 13+) — PATH'te erişilebilir olmalı
::   - server\ klasöründe sqlite3.c ve sqlite3.h mevcut olmalı
::
:: Kullanım: server\ klasöründen çalıştır  →  build.bat
:: ─────────────────────────────────────────────────────────────────────────────

setlocal

set OUTPUT=server.exe
set SRC=server.cpp sqlite3.c
set FLAGS=-std=c++17 -O2
set LIBS=-lpthread -lws2_32

:: MinGW dizinlerini ara
set GXX=

for %%G in (
    "C:\Qt\Tools\mingw1310_64\bin\g++.exe"
    "C:\mingw64\bin\g++.exe"
    "C:\MinGW\bin\g++.exe"
    "C:\msys64\mingw64\bin\g++.exe"
) do (
    if exist %%G (
        set GXX=%%G
        goto :found_gxx
    )
)

:: PATH'te de dene
where g++ >nul 2>&1
if %ERRORLEVEL% == 0 (
    set GXX=g++
    goto :found_gxx
)

echo.
echo  [HATA] g++ bulunamadi!
echo.
echo  Cozum secenekleri:
echo    1) Qt MinGW kurulu ise PATH'e ekle:
echo       set PATH=C:\Qt\Tools\mingw1310_64\bin;%%PATH%%
echo    2) MSYS2 kurun: https://www.msys2.org/
echo       pacman -S mingw-w64-x86_64-gcc
echo.
pause
exit /b 1

:found_gxx
echo  Derleyici: %GXX%

:: sqlite3.c mevcut mu?
if not exist "%~dp0sqlite3.c" (
    echo.
    echo  [HATA] sqlite3.c bulunamadi!
    echo  SQLite amalgamation'i indirin:
    echo    https://www.sqlite.org/download.html
    echo  sqlite3.c ve sqlite3.h dosyalarini bu klasore kopyalayin.
    echo.
    pause
    exit /b 1
)

echo  Kaynak: %SRC%
echo  Cikti : %OUTPUT%
echo.

:: sqlite3.c C kodu olduğundan önce C derleyiciyle object dosyasına çevrilir,
:: ardından C++ ile linklenir. Doğrudan g++ ile derlemeye çalışmak başarısız olur.
gcc -c sqlite3.c -o sqlite3.o
if %ERRORLEVEL% neq 0 ( echo [HATA] sqlite3.c derlenemedi! & pause & exit /b 1 )

%GXX% server.cpp sqlite3.o -o %OUTPUT% %FLAGS% %LIBS%

if %ERRORLEVEL% neq 0 (
    echo.
    echo  [HATA] Derleme basarisiz!
    pause
    exit /b 1
)

echo.
echo  [OK] Derleme tamamlandi: %~dp0%OUTPUT%
echo.
echo  Calistirmak icin: server.exe
echo  Port: 12345  ^|  Kullanici: admin  ^|  Sifre: admin123
echo.
pause
