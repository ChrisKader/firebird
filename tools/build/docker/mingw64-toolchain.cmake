set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

set(_mingw_sysroot /usr/x86_64-w64-mingw32/sys-root/mingw)
set(_qt_windows_root "$ENV{FIREBIRD_QT_WINDOWS_ROOT}")
set(_qt_host_root "$ENV{FIREBIRD_QT_HOST_ROOT}")

if(_qt_host_root)
    set(QT_HOST_PATH "${_qt_host_root}" CACHE PATH "Qt host tools path" FORCE)
endif()

set(CMAKE_FIND_ROOT_PATH "${_mingw_sysroot}")
if(_qt_windows_root)
    list(APPEND CMAKE_FIND_ROOT_PATH "${_qt_windows_root}")
endif()

set(CMAKE_PREFIX_PATH "${_mingw_sysroot}/lib/cmake")
if(_qt_windows_root)
    list(PREPEND CMAKE_PREFIX_PATH "${_qt_windows_root}")
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
