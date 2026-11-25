add_library(jjffe_platform
    video.cpp
    dpal_build.cpp
    dpal_table_globals.cpp
    dpal_luma.c
    sound.cpp
    input.cpp
    timer.cpp
    case_path.cpp
)

target_include_directories(jjffe_platform PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/data_blob
)

if(JJFFE_USE_SDL)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL REQUIRED sdl)
    target_compile_definitions(jjffe_platform PRIVATE JJFFE_USE_SDL)
    target_include_directories(jjffe_platform PRIVATE ${SDL_INCLUDE_DIRS})
endif()
