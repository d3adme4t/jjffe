cmake_minimum_required(VERSION 3.24)
project(jjffe VERSION 2.8 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# Compiler warnings — catch real bugs early
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

option(JJFFE_USE_SDL "Build with SDL backend" ON)
option(JJFFE_USE_OGRE "Build Ogre/Vulkan binary (ffelnxogre)" OFF)
option(JJFFE_USE_D3D9 "Build D3D9/DXVK binary (ffelnxd3d)" OFF)

# --- Build acceleration: ccache ---
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "ccache found: ${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
else()
    message(WARNING "ccache not found -- build will not be cached. Install ccache for faster rebuilds.")
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)

add_subdirectory(src/core)
add_subdirectory(src/platform)
add_subdirectory(src/game)
add_subdirectory(src/data)

add_executable(ffelnxsdl src/main.cpp)
target_link_libraries(ffelnxsdl
    $<LINK_GROUP:RESCAN,jjffe_game,jjffe_platform,jjffe_core,jjffe_data>
    
)

if(JJFFE_USE_SDL)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL REQUIRED sdl)
    
    # Phase 56: SDL_mixer for music playback
    pkg_check_modules(SDL_MIXER QUIET SDL_mixer)
    
    # Add SDL backend sources to main executable
    target_sources(ffelnxsdl PRIVATE 
        src/platform/sdl_backend.cpp
        src/platform/sdl_input.cpp
        src/platform/sdl_sound.cpp
        src/platform/ffe_sound_mix.cpp
        src/platform/song_table.cpp
        src/platform/sdl_timer.cpp
        hmp.cpp
    )
    target_compile_definitions(ffelnxsdl PRIVATE JJFFE_USE_SDL)
    target_link_libraries(ffelnxsdl ${SDL_LIBRARIES} GL)
    target_include_directories(ffelnxsdl PRIVATE ${SDL_INCLUDE_DIRS})
    if(SDL_MIXER_FOUND)
        target_link_libraries(ffelnxsdl ${SDL_MIXER_LIBRARIES})
        target_include_directories(ffelnxsdl PRIVATE ${SDL_MIXER_INCLUDE_DIRS})
        target_compile_definitions(ffelnxsdl PRIVATE JJFFE_USE_SDL_MIXER)
    endif()
    target_compile_definitions(ffelnxsdl PRIVATE __FFELNXSDL__)
endif()

# --- Optional FFED3D D3D9 / DXVK Native binary (ships .x + planets) ---
if(JJFFE_USE_D3D9)
    add_subdirectory(src/d3d)
    add_executable(ffelnxd3d src/main.cpp)
    target_link_libraries(ffelnxd3d
        $<LINK_GROUP:RESCAN,jjffe_d3d,jjffe_game,jjffe_platform,jjffe_core,jjffe_data>
    )
    target_compile_definitions(ffelnxd3d PRIVATE JJFFE_USE_D3D9 __FFELNXD3D__)
    set_target_properties(ffelnxd3d PROPERTIES
        BUILD_RPATH "/usr/local/lib64;/usr/local/lib"
        INSTALL_RPATH "/usr/local/lib64;/usr/local/lib"
    )
endif()

# --- Optional Ogre-Next 4 / Vulkan binary (menus + ship demo) ---
if(JJFFE_USE_OGRE)
    add_subdirectory(src/ogre)
    add_executable(ffelnxogre src/main.cpp)
    target_link_libraries(ffelnxogre
        $<LINK_GROUP:RESCAN,jjffe_ogre,jjffe_game,jjffe_platform,jjffe_core,jjffe_data>
    )
    target_compile_definitions(ffelnxogre PRIVATE JJFFE_USE_OGRE __FFELNXOGRE__)
    # RPATH matches JJFFE_OGRE_NEXT_ROOT (default /opt/ogre-next)
    if(NOT JJFFE_OGRE_NEXT_ROOT)
        set(JJFFE_OGRE_NEXT_ROOT "/opt/ogre-next")
    endif()
    set_target_properties(ffelnxogre PROPERTIES
        BUILD_RPATH "${JJFFE_OGRE_NEXT_ROOT}/lib"
        INSTALL_RPATH "${JJFFE_OGRE_NEXT_ROOT}/lib"
    )
    add_custom_command(TARGET ffelnxogre POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/data/ogre-next
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/data/ogre-next
            ${CMAKE_BINARY_DIR}/data/ogre-next
        COMMENT "Copy data/ogre-next (plugins + Hlms Media) into build tree"
    )
endif()

