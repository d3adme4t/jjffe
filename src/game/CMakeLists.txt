cmake_minimum_required(VERSION 3.10)
project(jjffe_game)

set(CMAKE_CXX_STANDARD 17)

add_library(jjffe_game
    draw.cpp
    band_lane_interp.cpp
    band_lane_tables.cpp
    ab_plotstamp.cpp
    ui.cpp
    state.cpp
    bytecode.cpp
    draw_main.cpp
    menu.cpp
    briefing.cpp
    demo3d.cpp
    intro_cast.cpp
    game_loop.cpp
    mission.cpp
    frame_pose.cpp
    logic.cpp
    ship_controls.cpp
    ai_behavior.cpp
    combat.cpp
    sysgen.cpp
    sysgen_proc.cpp
    terrain_query.cpp
    player_pad.cpp
    traffic_spawn.cpp
    world.cpp
    player_spawn.cpp
    body_paint.cpp
    ship_model.cpp
    mesh_parser.cpp
    vertex_transform.cpp
    ship_renderer.cpp
    ship_export.cpp
    ship_model_asset.cpp
    ship_face_semantic.cpp
    face_analysis.cpp
    face_inside.cpp
    export_approval.cpp
    planet_render.cpp
    contour_bezier.cpp
    contour_tessellate.cpp
    ${CMAKE_SOURCE_DIR}/third_party/clipper2/src/clipper.engine.cpp
    ${CMAKE_SOURCE_DIR}/third_party/clipper2/src/clipper.offset.cpp
    ${CMAKE_SOURCE_DIR}/third_party/clipper2/src/clipper.rectclip.cpp
)

target_include_directories(jjffe_game PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/data_blob
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/third_party/clipper2/include
)

find_package(ZLIB REQUIRED)
target_link_libraries(jjffe_game PUBLIC ZLIB::ZLIB)

# Enable test-only simulation functions in ship_controls
