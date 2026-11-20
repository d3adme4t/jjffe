# Ogre-Next 4 / Vulkan renderer (ffelnxogre)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

# Fixed prefix — no shell PKG_CONFIG_PATH required. Override with -DJJFFE_OGRE_NEXT_ROOT=...
set(JJFFE_OGRE_NEXT_ROOT "/opt/ogre-next" CACHE PATH "Ogre-Next 4 install prefix")
if(NOT EXISTS "${JJFFE_OGRE_NEXT_ROOT}/include/OGRE-Next/Ogre.h")
    message(FATAL_ERROR
        "Ogre-Next not found at ${JJFFE_OGRE_NEXT_ROOT} "
        "(expected include/OGRE-Next/Ogre.h). Set -DJJFFE_OGRE_NEXT_ROOT=...")
endif()
if(NOT EXISTS "${JJFFE_OGRE_NEXT_ROOT}/lib/libOgreNextMain.so")
    message(FATAL_ERROR
        "Ogre-Next libs missing under ${JJFFE_OGRE_NEXT_ROOT}/lib "
        "(expected libOgreNextMain.so)")
endif()

set(OGRE_NEXT_INCLUDE_DIRS
    ${JJFFE_OGRE_NEXT_ROOT}/include
    ${JJFFE_OGRE_NEXT_ROOT}/include/OGRE-Next
    ${JJFFE_OGRE_NEXT_ROOT}/include/OGRE-Next/Hlms/Common
    ${JJFFE_OGRE_NEXT_ROOT}/include/OGRE-Next/Hlms/Unlit
)
set(OGRE_NEXT_LIBRARY_DIRS ${JJFFE_OGRE_NEXT_ROOT}/lib)
message(STATUS "Ogre-Next: ${JJFFE_OGRE_NEXT_ROOT}")

add_library(jjffe_ogre STATIC
    ogre_backend.cpp
    ogre_ui.cpp
    ogre_font.cpp
    ogre_demo.cpp
    ogre_texture.cpp
    ogre_soft_blit.cpp
    ogre_ship_hw.cpp
)

target_include_directories(jjffe_ogre PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
)
# Treat Ogre-Next as a system dependency so its headers don't inject
# -Wall/-Wextra noise (e.g. -Woverloaded-virtual) into our build.
target_include_directories(jjffe_ogre SYSTEM PUBLIC
    ${OGRE_NEXT_INCLUDE_DIRS}
)

target_compile_definitions(jjffe_ogre PUBLIC
    JJFFE_USE_OGRE
)

target_link_directories(jjffe_ogre PUBLIC
    ${OGRE_NEXT_LIBRARY_DIRS}
)

# Ogre-Next Vulkan plugin was built without NEEDED on glslang/SPIRV; pull them
# into the executable so DynLib::load can resolve TProgram::getInfoLog etc.
target_link_libraries(jjffe_ogre PUBLIC
    OgreNextHlmsUnlit
    OgreNextMain
    glslang
    SPIRV
    ${SDL2_LIBRARIES}
)
# Ensure unused deps are still linked (plugin resolves from the process).
target_link_options(jjffe_ogre PUBLIC "LINKER:--no-as-needed")
