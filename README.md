# so_drive

A TCP-based remote file manager with a Qt6 GUI client and a plain C++ server.  
The client runs on **Windows**; the server runs on **Linux**.

---

## Features

- Login with IP, port, username, and password (with "Remember Me")
- Directory browsing with back/forward navigation
- File upload and download
- Built-in text editor for remote files (`.txt`, `.cpp`, `.py`, `.json`, etc.)
- Create/delete files and folders remotely
- File-type icons for common formats
- Per-user sandbox — each user is restricted to their own folder on the server

---

## Project Structure

```
so_drive/
├── main.cpp
├── mainwindow.*             # Login screen
├── filewindow.*             # File manager screen
├── dialognewitem.*          # New file/folder dialog
├── resources.qrc
├── images/                  # File-type icons
├── so_drive.pro             # qmake build file
├── CMakeLists.txt           # CMake build file
├── server/
│   └── server.cpp           # Plain C++ TCP server (Linux)
├── build.bat                # One-click build script (Windows)
└── setup.bat                # Qt environment installer (Windows)
```

---

## TCP Protocol

| Command | Description |
|---|---|
| `LOGIN\|user\|pass` | Authenticate |
| `LIST_FILES` | List current directory |
| `CD\|dirname` | Change directory |
| `CD\|..` | Go up one level |
| `CAT\|filename` | Read file content |
| `DOWNLOAD\|filename` | Download a file |
| `UPLOAD\|filename\|size` | Upload a file |
| `DELETE\|filename` | Delete a file or folder |
| `TOUCH\|filename` | Create a new file |
| `MKDIR\|dirname` | Create a new folder |
| `WRITE\|filename\|content` | Save file content |

---

## Client — Build (Windows)

> **QtCreator is not required.** Only the Qt libraries and MinGW compiler are needed.

### Step 1 — Install Qt (first time only)

Run `setup.bat`. It uses `aqtinstall` to download Qt 6.10.1 and MinGW automatically (~600 MB, no IDE).

```
setup.bat
```

Requirements: Python 3.x must be installed.

### Step 2 — Build

```
build.bat
```

Output: `build\release-cli\release\so_drive.exe` (with all Qt DLLs bundled by windeployqt)

### Manual build (qmake)

```bat
set PATH=C:\Qt\6.10.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;%PATH%
mkdir build && cd build
qmake ..\so_drive.pro -spec win32-g++ "CONFIG+=release"
mingw32-make -j4
windeployqt release\so_drive.exe
```

### CMake (alternative)

```bash
cmake -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.10.1/mingw_64
cmake --build build --config Release
```

### Editing UI files

Use **Qt Designer** (no QtCreator needed):

```
C:\Qt\6.10.1\mingw_64\bin\designer.exe
```

---

## Server — Build (Linux)

The server uses POSIX sockets, C++17 filesystem, and SQLite3.

### Dependencies

```bash
# Debian/Ubuntu
sudo apt install g++ libsqlite3-dev

# Arch
sudo pacman -S gcc sqlite
```

### Compile

```bash
cd server
g++ server.cpp -o server -std=c++17 -lpthread -lsqlite3
```

### Run

```bash
./server
# Listens on port 12345 by default
```

### Database

The server expects a SQLite database file named `User_Informations.sqlite` in the same directory, with the following schema:

```sql
CREATE TABLE UserInformation (
    Username   TEXT NOT NULL,
    Password   TEXT NOT NULL,
    UserFolder TEXT NOT NULL
);
```

`UserFolder` is the absolute path to the user's sandbox directory on the server.

---

## Dependencies Summary

| Component | Version | Purpose |
|---|---|---|
| Qt | 6.10.1 | Client GUI framework |
| MinGW | 13.1.0 64-bit | C++ compiler (Windows) |
| Python + aqtinstall | any | Qt installer script (one-time) |
| GCC | any C++17 | Server compiler (Linux) |
| libsqlite3 | any | Server authentication |
