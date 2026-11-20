# FFED3D-style D3D9 renderer via DXVK Native + SDL2 (ffelnxd3d)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(ASSIMP REQUIRED assimp)

set(JJFFE_DXVK_ROOT "/usr/local" CACHE PATH "DXVK Native install prefix")
set(JJFFE_DXVK_INCLUDE_DIR "${JJFFE_DXVK_ROOT}/include/dxvk" CACHE PATH "DXVK DirectX headers")
set(JJFFE_DXVK_LIB_DIR "${JJFFE_DXVK_ROOT}/lib64" CACHE PATH "DXVK library dir")

if(NOT EXISTS "${JJFFE_DXVK_INCLUDE_DIR}/d3d9.h")
    # Fallback to source tree headers (native WSI layout)
    if(EXISTS "/home/alex/Src/dxvk/include/native/directx/d3d9.h")
        set(JJFFE_DXVK_INCLUDE_DIR "/home/alex/Src/dxvk/include/native/directx")
        set(JJFFE_DXVK_NATIVE_INCLUDE "/home/alex/Src/dxvk/include/native")
    else()
        message(FATAL_ERROR
            "DXVK d3d9.h not found at ${JJFFE_DXVK_INCLUDE_DIR}. "
            "Set -DJJFFE_DXVK_ROOT=... or install DXVK Native.")
    endif()
endif()

if(NOT EXISTS "${JJFFE_DXVK_LIB_DIR}/libdxvk_d3d9.so")
    if(EXISTS "/usr/local/lib/libdxvk_d3d9.so")
        set(JJFFE_DXVK_LIB_DIR "/usr/local/lib")
    elseif(EXISTS "/home/alex/Src/dxvk/build-native/src/d3d9/libdxvk_d3d9.so")
        set(JJFFE_DXVK_LIB_DIR "/home/alex/Src/dxvk/build-native/src/d3d9")
    else()
        message(FATAL_ERROR "libdxvk_d3d9.so not found. Set -DJJFFE_DXVK_LIB_DIR=...")
    endif()
endif()

message(STATUS "DXVK include: ${JJFFE_DXVK_INCLUDE_DIR}")
message(STATUS "DXVK lib:     ${JJFFE_DXVK_LIB_DIR}")

add_library(jjffe_d3d STATIC
    d3d_backend.cpp
    d3d_mesh.cpp
    d3d_ship_asset.cpp
    d3d_demo.cpp
)

target_include_directories(jjffe_d3d PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
    ${SDL2_INCLUDE_DIRS}
    ${ASSIMP_INCLUDE_DIRS}
    ${JJFFE_DXVK_INCLUDE_DIR}
)
if(DEFINED JJFFE_DXVK_NATIVE_INCLUDE)
    target_include_directories(jjffe_d3d PUBLIC ${JJFFE_DXVK_NATIVE_INCLUDE})
endif()

target_compile_definitions(jjffe_d3d PUBLIC
    JJFFE_USE_D3D9
)

target_link_directories(jjffe_d3d PUBLIC
    ${JJFFE_DXVK_LIB_DIR}
    ${ASSIMP_LIBRARY_DIRS}
)

target_link_libraries(jjffe_d3d PUBLIC
    dxvk_d3d9
    ${SDL2_LIBRARIES}
    ${ASSIMP_LIBRARIES}
)

set_target_properties(jjffe_d3d PROPERTIES
    BUILD_RPATH "${JJFFE_DXVK_LIB_DIR}"
    INSTALL_RPATH "${JJFFE_DXVK_LIB_DIR}"
)
