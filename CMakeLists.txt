cmake_minimum_required(VERSION 3.30...4.2)

project(boxpacker_showcase
    HOMEPAGE_URL "https://github.com/InCom-0/BoxPacker_showcase"
    VERSION "0.0.1"
    DESCRIPTION
    "Showcase for BoxPacker algorithms"
    LANGUAGES CXX C)

set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

option(CPM_USE_LOCAL_PACKAGES "Use local (usually system installed) packages for dependencies." ON)
option(CPM_LOCAL_PACKAGES_ONLY "Forbid the build system from downloading missing dependencies." OFF)

option(boxpacker_showcase_ENABLE_LTO "Request LTO pending support" OFF)

option(boxpacker_showcase_STAGE_OUTPUTS "Stage build output artifacts into proper directory structure" ON)
option(boxpacker_showcase_PORTABLE_LAYOUT "Use flat (Windows-like / portable) install layout" OFF)
set(boxpacker_showcase_EMSCRIPTEN_PTHREAD_POOL_SIZE "16" CACHE STRING "Emscripten worker pool size used for pthreads")
set(boxpacker_showcase_EMSCRIPTEN_PTHREAD_POOL_SIZE_STRICT "2" CACHE STRING "Emscripten pthread pool exhaustion behavior (2 = throw explicit error)")


#####################################################################
### Staging and Install dirs setting ###
#####################################################################
include(cmake/incom/StageOutputs.cmake)
include(cmake/incom/MingwRuntimeDeps.cmake)

include(GNUInstallDirs)
set(boxpacker_showcase_PORTABLE_LAYOUT_INSTALL_DIR "/" CACHE PATH "Dir under prefix to install to in portable layout mode")

if(boxpacker_showcase_STAGE_OUTPUTS)
    stageOutputDirs(${PROJECT_NAME} ${boxpacker_showcase_PORTABLE_LAYOUT})
endif()


#####################################################################
### Platform specifics + hacks + LTO ###
#####################################################################
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT MATCHES "MSVC")
    add_compile_options(/utf-8)
    add_compile_definitions(NOMINMAX)
endif()


### This attempts to compile sample program to find out if we are using libc++(USING_LIBCXX) or libstdc++(USING_LIBSTDCXX) or MSVC_STL(USING_MSVC_STL)
include(cmake/incom/StdlibQuery.cmake)

include(CheckIPOSupported)
check_ipo_supported(RESULT boxpacker_showcase_LTO OUTPUT boxpacker_showcase_LTO_OUTPUT)

# Enable LTO if supported and only when NOT in debug mode
if(boxpacker_showcase_ENABLE_LTO)
    if(boxpacker_showcase_LTO)
        message(STATUS "LTO is supported")
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)
            message(STATUS "LTO is forced disabled because of Debug build")
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
            set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)
            message(STATUS "LTO is forced disabled on MacOS")
        else()
            set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
            message(STATUS "LTO is enabled")
            # set_property(TARGET foo PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        endif()

    else()
        message(WARNING "LTO is NOT supported: ${boxpacker_showcase_LTO_OUTPUT}")
    endif()
else()
    message(STATUS "LTO is disabled by the user")
endif()


######################################################################
### Target specification ###
######################################################################
add_executable(boxpacker_showcase)
target_sources(boxpacker_showcase PRIVATE src/main.cpp)

target_sources(boxpacker_showcase
    PRIVATE
    FILE_SET priv_headers
    TYPE HEADERS
    BASE_DIRS
    src/private_inc/)

target_compile_features(boxpacker_showcase PRIVATE cxx_std_26)

if(EMSCRIPTEN)
    set(boxpacker_showcase_EMSCRIPTEN_SHELL_FILE
        "${CMAKE_SOURCE_DIR}/web/shell_minimal.html")
    set(boxpacker_showcase_EMSCRIPTEN_SAMPLE_INPUT
        "${CMAKE_SOURCE_DIR}/data/BoxPacker_sample_input.txt")

    set_target_properties(boxpacker_showcase PROPERTIES
        OUTPUT_NAME "index"
        SUFFIX ".html")

    target_link_options(boxpacker_showcase PRIVATE
        "SHELL:-sEXPORTED_RUNTIME_METHODS=[]"
        "SHELL:--shell-file ${boxpacker_showcase_EMSCRIPTEN_SHELL_FILE}"
        "SHELL:--preload-file ${boxpacker_showcase_EMSCRIPTEN_SAMPLE_INPUT}@/data/BoxPacker_sample_input.txt"
        "-sALLOW_MEMORY_GROWTH=1"
        "$<$<CONFIG:Debug>:-sASSERTIONS=2>")
endif()


##########################################################################
### Settings the right options for linking for Windows and Apple  ###
##########################################################################


##########################################################################
### Dependencies and library linking  ###
##########################################################################
include(CMake_dependencies.cmake) ### Loads and declares the required external libraries using CPM or CMake's FetchContent.

if(EMSCRIPTEN AND TARGET imgui-opengl3)
    foreach(_prop IN ITEMS INTERFACE_LINK_LIBRARIES LINK_LIBRARIES)
        get_target_property(_imgui_gl_libs imgui-opengl3 ${_prop})
        if(_imgui_gl_libs)
            list(FILTER _imgui_gl_libs EXCLUDE REGEX "^(OpenGL::GL|GL|-lGL)$")
            set_target_properties(imgui-opengl3 PROPERTIES ${_prop} "${_imgui_gl_libs}")
        endif()
    endforeach()
endif()

target_link_libraries(boxpacker_showcase PRIVATE incstd::incstd ctre::ctre)
target_link_libraries(boxpacker_showcase PRIVATE imgui imgui-sdl2 imgui-opengl3)
target_link_libraries(boxpacker_showcase PRIVATE STDEXEC::stdexec readerwriterqueue)

if(EMSCRIPTEN)
    target_compile_options(boxpacker_showcase PUBLIC -sUSE_SDL=2)
    target_compile_options(boxpacker_showcase PUBLIC -pthread -fexceptions -fPIC)
    target_link_options(boxpacker_showcase PUBLIC
        --emrun
        -sEXPORTED_RUNTIME_METHODS=[ccall,HEAPU8]
        -sEXPORTED_FUNCTIONS=[_main,_malloc,_free]
        -sWASM=1
        -sALLOW_MEMORY_GROWTH=1
        -sDISABLE_EXCEPTION_CATCHING=0
        -sPTHREAD_POOL_SIZE=${boxpacker_showcase_EMSCRIPTEN_PTHREAD_POOL_SIZE}
        -sPTHREAD_POOL_SIZE_STRICT=${boxpacker_showcase_EMSCRIPTEN_PTHREAD_POOL_SIZE_STRICT}
        -pthread
        -sMALLOC=mimalloc
    )
endif()


if(USING_LIBSTDCXX)
    target_link_libraries(boxpacker_showcase PRIVATE "-lstdc++exp")
endif()


############################################################################################
### Automatic copying certain assets to build directory (mimic structure as installed)   ###
############################################################################################
if(boxpacker_showcase_PORTABLE_LAYOUT)
    add_custom_command(TARGET boxpacker_showcase POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Recreating portable installation file structure in build staging directory"

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/README.md"
        "${${PROJECT_NAME}_STAGEDIR}/${boxpacker_showcase_PORTABLE_LAYOUT_INSTALL_DIR}/README.md"

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/LICENSE.txt"
        "${${PROJECT_NAME}_STAGEDIR}/${boxpacker_showcase_PORTABLE_LAYOUT_INSTALL_DIR}/LICENSE.txt"
    )

else()
    ### '.incplot-dev-build' gets created next to the binary in 'CMAKE_RUNTIME_OUTPUT_DIRECTORY'
    ### The above enables running incplot from inside build directory (without installing it)
    add_custom_command(TARGET boxpacker_showcase POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Recreating GNUInstallDirs installation file structure in build staging directory"

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/README.md"
        "${${PROJECT_NAME}_STAGEDIR}/${CMAKE_INSTALL_DOCDIR}/README.md"

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/LICENSE.txt"
        "${${PROJECT_NAME}_STAGEDIR}/${CMAKE_INSTALL_DATAROOTDIR}/licenses/${PROJECT_NAME}/LICENSE.txt"
    )
endif()


########################################################
### Install specification ###
########################################################
if(boxpacker_showcase_PORTABLE_LAYOUT)
    install(TARGETS boxpacker_showcase
        RUNTIME DESTINATION ${boxpacker_showcase_PORTABLE_LAYOUT_INSTALL_DIR}
    )
    install(FILES $<TARGET_RUNTIME_DLLS:boxpacker_showcase>
        DESTINATION ${boxpacker_showcase_PORTABLE_LAYOUT_INSTALL_DIR}
        OPTIONAL
    )

    install(FILES README.md DESTINATION ${boxpacker_showcase_PORTABLE_LAYOUT_INSTALL_DIR})
    install(FILES LICENSE.txt DESTINATION "${boxpacker_showcase_PORTABLE_LAYOUT_INSTALL_DIR}")

else()
    install(TARGETS boxpacker_showcase
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(FILES $<TARGET_RUNTIME_DLLS:boxpacker_showcase>
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        OPTIONAL
    )

    install(FILES README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
    install(FILES LICENSE.txt DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/${PROJECT_NAME}/")
endif()
