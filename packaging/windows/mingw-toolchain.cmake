# CMake toolchain for cross-compiling KumCAD from Linux to Windows x86_64
# using the system's mingw-w64-gcc cross-compiler against a Qt for Windows
# (MinGW) kit fetched separately via aqtinstall (see build-windows.sh).
#
# Usage: cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=packaging/windows/mingw-toolchain.cmake \
#              -DCMAKE_PREFIX_PATH=/path/to/winqt/6.9.3/mingw_64

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(MINGW_TRIPLE x86_64-w64-mingw32)

set(CMAKE_C_COMPILER ${MINGW_TRIPLE}-gcc)
set(CMAKE_CXX_COMPILER ${MINGW_TRIPLE}-g++)
set(CMAKE_RC_COMPILER ${MINGW_TRIPLE}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${MINGW_TRIPLE})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
