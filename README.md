# so_drive

A Qt6 desktop GUI client for browsing and managing files on a remote server over TCP.

## Features

- Login with IP, port, username, and password (with "Remember Me")
- Directory browsing with back/forward navigation
- File upload and download
- Built-in text editor for remote files (`.txt`, `.cpp`, `.py`, `.json`, etc.)
- Create/delete files and folders
- File-type icons for common formats

## Protocol

The client communicates with the server using a simple text-based TCP protocol:

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

## Requirements

- Qt 6.x (Core, Gui, Widgets, Network modules)
- CMake 3.16+ **or** qmake (included with Qt)
- C++17 compiler (MinGW, MSVC, GCC, Clang)

## Build with CMake (recommended)

```bash
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/Qt6
cmake --build build
```

On Windows with Qt installed via the Qt installer, `CMAKE_PREFIX_PATH` is typically something like `C:/Qt/6.x.x/mingw_64`.

## Build with qmake

```bash
qmake so_drive.pro
make          # Linux/macOS
mingw32-make  # Windows (MinGW)
```

## Usage

Run the compiled executable and connect to a compatible server that implements the protocol described above.
