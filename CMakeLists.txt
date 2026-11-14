cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
    set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
endif()

# Statically link MSVC runtime on Windows so open_city.exe runs on any PC
# without requiring a separate VC++ Redistributable install.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(open_city VERSION 0.9.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OPEN_CITY_BUILD_APP "Build the 3D OpenGL game executable" ON)
option(OPEN_CITY_BUILD_TESTS "Build the simulation unit & integration tests" ON)

find_program(RUSTC_EXECUTABLE rustc)
set(OPEN_CITY_RUST_OBJECTS "")
if(UNIX AND RUSTC_EXECUTABLE)
    set(RUST_PHYSICS_OBJ "${CMAKE_CURRENT_BINARY_DIR}/physics_kernel.o")
    add_custom_command(
        OUTPUT "${RUST_PHYSICS_OBJ}"
        COMMAND "${RUSTC_EXECUTABLE}"
            --crate-type=lib
            --emit=obj
            -C opt-level=3
            -C panic=abort
            "${CMAKE_CURRENT_SOURCE_DIR}/src/physics/physics_kernel.rs"
            -o "${RUST_PHYSICS_OBJ}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/physics/physics_kernel.rs"
        COMMENT "Compiling native Rust physics kernel (src/physics/physics_kernel.rs)"
        VERBATIM
    )
    set_source_files_properties("${RUST_PHYSICS_OBJ}" PROPERTIES
        EXTERNAL_OBJECT TRUE
        GENERATED TRUE
    )
    list(APPEND OPEN_CITY_RUST_OBJECTS "${RUST_PHYSICS_OBJ}")
endif()

# Pure C++20 + Rust simulation core (city generation, 6-speed gearbox, 4-wheel suspension,
# rotational rigid-body collisions, stunt ramps, traffic/pedestrian/gang AI, weapons, shops)
add_library(open_city_core STATIC
    src/world/CityMap.cpp
    src/gameplay/GameWorld.cpp
    ${OPEN_CITY_RUST_OBJECTS}
)

if(UNIX AND RUSTC_EXECUTABLE)
    target_compile_definitions(open_city_core PUBLIC OC_USE_RUST_PHYSICS=1)
endif()

target_include_directories(open_city_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
    target_compile_options(open_city_core PUBLIC /utf-8 /W4 /permissive-)
    target_compile_definitions(open_city_core PUBLIC NOMINMAX _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(open_city_core PUBLIC -Wall -Wextra -Wpedantic)
endif()

if(OPEN_CITY_BUILD_APP)
    include(FetchContent)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.5
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(raylib)

    add_executable(open_city
        src/main.cpp
        src/audio/SynthAudio.cpp
        src/render/Renderer3D.cpp
        src/ui/HudRenderer.cpp
    )

    target_link_libraries(open_city PRIVATE open_city_core raylib)

    if(MSVC)
        target_compile_options(open_city PRIVATE /utf-8 /W4 /permissive-)
        target_link_options(open_city PRIVATE "/SUBSYSTEM:WINDOWS" "/ENTRY:mainCRTStartup")
    elseif(MINGW)
        target_compile_options(open_city PRIVATE -Wall -Wextra -Wpedantic)
        target_link_options(open_city PRIVATE -mwindows -static -static-libgcc -static-libstdc++)
    else()
        target_compile_options(open_city PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(OPEN_CITY_BUILD_TESTS)
    enable_testing()
    add_executable(open_city_tests
        tests/test_simulation.cpp
    )
    target_link_libraries(open_city_tests PRIVATE open_city_core)
    if(MSVC)
        target_compile_options(open_city_tests PRIVATE /utf-8 /W4 /permissive-)
    else()
        target_compile_options(open_city_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME simulation_tests COMMAND open_city_tests)
endif()
