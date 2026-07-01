include(cmake/CPM_0.42.3.cmake)


CPMAddPackage("gh:InCom-0/incstd#main")
CPMAddPackage("gh:hanickadot/compile-time-regular-expressions@3.11.0")


CPMAddPackage(
    URL https://github.com/cameron314/readerwriterqueue/archive/refs/tags/v1.0.7.tar.gz
    URL_HASH SHA256=532224ed052bcd5f4c6be0ed9bb2b8c88dfe7e26e3eb4dd9335303b059df6691
    EXCLUDE_FROM_ALL TRUE
    NAME readerwriterqueue
)
CPMAddPackage(
    URL https://github.com/NVIDIA/stdexec/archive/refs/tags/nvhpc-26.05.tar.gz
    URL_HASH SHA256=9d2396fecd604698c1eae58f0cb6e4517aa727013846240d1a7b2f35e49884dc
    EXCLUDE_FROM_ALL TRUE
    NAME stdexec
)


#####################################################################
### ImGui related ###
#####################################################################
# CPMAddPackage(
#     NAME Freetype
#     GIT_REPOSITORY https://gitlab.freedesktop.org/freetype/freetype.git
#     GIT_TAG VER-2-14-3
#     VERSION 2.14.3
# )

# if(Freetype_ADDED AND NOT TARGET Freetype::Freetype)
#     add_library(Freetype::Freetype ALIAS freetype)
# endif()


#################################################################################################
### SDL2 ... for when we will be able to compile with MSVC (when MSVC supports pack indexing) ###
#################################################################################################
# CPMAddPackage(
#   NAME SDL2
#   URL "https://github.com/libsdl-org/SDL/archive/refs/tags/release-2.32.10.tar.gz"
#   URL_HASH SHA256=03f9d7c191a837525c9cda6406af2f2e48be02b5e7eb03d949cc9f1e9ca41c8b
#   VERSION 2.32.10
#   EXCLUDE_FROM_ALL TRUE
# )


# ImGui
CPMAddPackage(
    NAME imgui
    VERSION 1.92.8
    GITHUB_REPOSITORY ocornut/imgui
    DOWNLOAD_ONLY TRUE
)

# CMakeLists.txt from https://gist.githubusercontent.com/rokups/f771217b2d530d170db5cb1e08e9a8f4
file(
    DOWNLOAD
    "https://gist.githubusercontent.com/rokups/f771217b2d530d170db5cb1e08e9a8f4/raw/4c2c14374ab878ca2f45daabfed4c156468e4e27/CMakeLists.txt"
    "${imgui_SOURCE_DIR}/CMakeLists.txt"
    EXPECTED_HASH SHA256=fd62f69364ce13a4f7633a9b50ae6672c466bcc44be60c69c45c0c6e225bb086
)

# Options
set(IMGUI_EXAMPLES FALSE)
set(IMGUI_DEMO FALSE)
set(IMGUI_ENABLE_STDLIB_SUPPORT TRUE)
# FreeType (https://github.com/cpm-cmake/CPM.cmake/wiki/More-Snippets#freetype)

set(IMGUI_ENABLE_FREETYPE FALSE)
set(FREETYPE_FOUND TRUE)
set(FREETYPE_INCLUDE_DIRS "")
set(FREETYPE_LIBRARIES Freetype::Freetype)

# Add subdirectory
set(IMGUI_IMPL_GLFW OFF)
set(IMGUI_IMPL_GLUT OFF)
set(IMGUI_IMPL_VULKAN OFF)
set(IMGUI_ENABLE_STDLIB_SUPPORT ON)
set(IMGUI_IMPL_OPENGL2 OFF)

add_subdirectory(${imgui_SOURCE_DIR} EXCLUDE_FROM_ALL SYSTEM)

unset(IMGUI_IMPL_GLFW)
unset(IMGUI_IMPL_GLUT)
unset(IMGUI_IMPL_VULKAN)
unset(IMGUI_ENABLE_STDLIB_SUPPORT)
unset(IMGUI_IMPL_OPENGL2)
