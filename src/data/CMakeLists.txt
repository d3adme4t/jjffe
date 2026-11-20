add_library(jjffe_data
    bmp_data.cpp
    mission_data.cpp
    data_blob.cpp
    data_loader.cpp
    bitmap_parser.cpp
    mission_data_integration.cpp
)

target_include_directories(jjffe_data PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/data_blob
)
