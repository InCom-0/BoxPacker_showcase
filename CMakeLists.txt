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

option(boxpacker_showcase_STAGE_OUTPUTS "Stage build output artifacts into proper directory structure" ON)
option(boxpacker_showcase_PORTABLE_LAYOUT "Use flat (Windows-like / portable) install layout" OFF)


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
if(boxpacker_showcase_LTO)
    message(STATUS "LTO is supported")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)
        message(STATUS "LTO is disabled because of Debug build")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)
        message(STATUS "LTO is disabled on MacOS")
    else()
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
        message(STATUS "LTO is enabled")
        # set_property(TARGET foo PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()

else()
    message(WARNING "LTO is NOT supported: ${boxpacker_showcase_LTO_OUTPUT}")
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
    include)

set(boxpacker_showcase_GENERATED_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
# include(cmake/config/configScript.cmake)

target_sources(boxpacker_showcase
    PRIVATE
    FILE_SET priv_auto_config
    TYPE HEADERS
    BASE_DIRS
    "${boxpacker_showcase_GENERATED_CONFIG_DIR}")


target_compile_features(boxpacker_showcase PRIVATE cxx_std_23)


##########################################################################
### Settings the right options for linking for Windows and Apple  ###
##########################################################################
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(CMAKE_SYSTEM_NAME MATCHES "Windows")
        set(BUILD_SHARED_LIBS OFF)
        if(USING_LIBSTDCXX)
            target_link_options(boxpacker_showcase PUBLIC -static)
        elseif(USING_LIBCXX)
            target_link_options(boxpacker_showcase PUBLIC -stdlib=libc++ -static)
        elseif(USING_MSVC_STL)
        endif()

    elseif(APPLE)
        if(USING_LIBSTDCXX)
            target_link_options(boxpacker_showcase PUBLIC -static)
        elseif(USING_LIBCXX)
            target_link_options(boxpacker_showcase PUBLIC -stdlib=libc++)
        elseif(USING_MSVC_STL)
            message(FATAL_ERROR "CMake reports we are using MSVC STL on APPLE platform. That is impossible.")
        endif()
    else()
    endif()

elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
endif()


##########################################################################
### Dependencies and library linking  ###
##########################################################################
include(CMake_dependencies.cmake) ### Loads and declares the required external libraries using CPM or CMake's FetchContent.

target_link_libraries(boxpacker_showcase PRIVATE incstd::incstd)


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