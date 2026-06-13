# so_drive

A TCP-based remote file manager with a Qt6 GUI client and a plain C++ server.  
The client runs on **Windows**. The server runs on **Linux, macOS, or Windows**.

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
│   ├── server.cpp           # C++ TCP server (cross-platform)
│   ├── sqlite3.c            # SQLite amalgamation (bundled, no install needed)
│   ├── sqlite3.h
│   └── build.bat            # One-click server build (Windows/MinGW)
├── build.bat                # One-click client build script (Windows)
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

## Server — Build

The server is cross-platform. SQLite is bundled (`sqlite3.c`) — no package manager needed.

### Linux / macOS

```bash
cd server
make
./server
```

`make` automatically downloads the SQLite amalgamation on first run (requires `curl` or `wget` and `unzip`). No package manager needed.

### Windows (MinGW)

Option A — use the batch script:

```
cd server
build.bat
```

Option B — manually (MinGW must be in PATH):

```bat
set PATH=C:\Qt\Tools\mingw1310_64\bin;%PATH%
cd server
gcc -c sqlite3.c -o sqlite3.o
g++ server.cpp sqlite3.o -o server.exe -std=c++17 -lpthread -lws2_32
server.exe
```

### First Run — Default User

On first startup the server automatically creates a SQLite database and a default user:

| Username | Password | Folder |
|---|---|---|
| `admin` | `admin123` | `./users/admin` |

To add more users, open `server.cpp`, uncomment the `addUser(...)` line in `main()`, rebuild once, then comment it out again.

### Connecting Over the Internet (ngrok)

```bash
# On the server machine:
ngrok tcp 12345

# Copy the forwarding address (e.g. 0.tcp.ngrok.io:12345)
# Enter it as IP + port in the client login screen
```

---

## Dependencies Summary

| Component | Version | Purpose |
|---|---|---|
| Qt | 6.10.1 | Client GUI framework |
| MinGW | 13.1.0 64-bit | C++ compiler (Windows client) |
| Python + aqtinstall | any | Qt installer script (one-time setup) |
| GCC / MinGW | C++17 | Server compiler (Linux / Windows) |
| SQLite amalgamation | 3.x | Bundled in `server/` — no install needed |
