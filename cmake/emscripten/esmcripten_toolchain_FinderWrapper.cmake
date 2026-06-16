# Resolve Emscripten root in this order:
# 1) EMSCRIPTEN env var
# 2) EMSDK/upstream/emscripten (Windows emsdk layout)
# 3) /usr/lib/emscripten (Linux/WSL distro layout)

if(DEFINED ENV{EMSCRIPTEN} AND NOT "$ENV{EMSCRIPTEN}" STREQUAL "")
    set(_EMSCRIPTEN_ROOT "$ENV{EMSCRIPTEN}")
elseif(DEFINED ENV{EMSDK} AND NOT "$ENV{EMSDK}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{EMSDK}/upstream/emscripten" _EMSCRIPTEN_ROOT)
elseif(EXISTS "/usr/lib/emscripten/cmake/Modules/Platform/Emscripten.cmake")
    set(_EMSCRIPTEN_ROOT "/usr/lib/emscripten")
else()
    message(FATAL_ERROR
        "Could not locate Emscripten.\n"
        "Set EMSCRIPTEN, or set EMSDK (emsdk), or install to /usr/lib/emscripten.")
endif()

set(_EMSCRIPTEN_TOOLCHAIN "${_EMSCRIPTEN_ROOT}/cmake/Modules/Platform/Emscripten.cmake")
if(NOT EXISTS "${_EMSCRIPTEN_TOOLCHAIN}")
    message(FATAL_ERROR "Emscripten toolchain not found: ${_EMSCRIPTEN_TOOLCHAIN}")
endif()


include("${_EMSCRIPTEN_TOOLCHAIN}")