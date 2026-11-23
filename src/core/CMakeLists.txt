add_library(jjffe_core
    cdr_file.cpp
    core.cpp
    data_lod_globals.cpp
    sysgen_shared_tables.cpp
    material_table_globals.cpp
    planet_icosa_draw.cpp
    planet_shade_setup.cpp
    planet_ab.cpp
    planet_midpoint_trace.c
    planet_mid_scr_trace.c
    planet_mid_else_trace.c
    planet_792_chain_trace.c
    planet_paint_gate_trace.c
    planet_root_seed.c
    planet_dpal_probe.c
    planet_shade_trace.c
    planet_subdiv_trace.c
    planet_sat_trace.c
    string.cpp
    math.cpp
    file_system.cpp
    misc_stubs.cpp
    sound_callback.cpp
    sha256.cpp
    job_trace.c
    vertex_trace.c
)

# Allow implicit pointer/integer conversions (decompiled code has many)
target_compile_options(jjffe_core PRIVATE -fpermissive)

# GCC ICEs on this 50k-line TU at -O3 (internal compiler error, final_scan_insn);
# compile it unoptimized until the toolchain is fixed.
set_source_files_properties(core.cpp PROPERTIES COMPILE_OPTIONS "-O0;-funsigned-char")

target_include_directories(jjffe_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/data_blob
)
