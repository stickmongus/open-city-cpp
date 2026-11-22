cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
    set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
endif()

# Statically link MSVC runtime on Windows so open_city.exe runs on any PC
# without requiring a separate VC++ Redistributable install.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(open_city VERSION 5.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OPEN_CITY_BUILD_APP "Build the Native Vulkan 3D Seoul executable / Android shared library" ON)
option(OPEN_CITY_BUILD_TESTS "Build the simulation unit & integration tests" ON)

find_program(RUSTC_EXECUTABLE rustc)
set(OPEN_CITY_RUST_OBJECTS "")
if(UNIX AND NOT ANDROID AND RUSTC_EXECUTABLE)
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
        COMMENT "Compiling native Rust physics kernel v2.0.0 (src/physics/physics_kernel.rs)"
        VERBATIM
    )
    set_source_files_properties("${RUST_PHYSICS_OBJ}" PROPERTIES
        EXTERNAL_OBJECT TRUE
        GENERATED TRUE
    )
    list(APPEND OPEN_CITY_RUST_OBJECTS "${RUST_PHYSICS_OBJ}")
endif()

# Pure C++20 + Rust simulation core (Real OpenStreetMap Seoul GIS + SRTM 30m DEM,
# 25k real roads, 107k real building polygons, realistic car physics, escapable police AI)
add_library(open_city_core STATIC
    src/world/RealSeoulData.cpp
    src/world/CityMap.cpp
    src/gameplay/GameWorld.cpp
    ${OPEN_CITY_RUST_OBJECTS}
)

if(UNIX AND NOT ANDROID AND RUSTC_EXECUTABLE)
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
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

    # 1. Khronos Official Vulkan Headers (header-only)
    FetchContent_Declare(
        vulkan_headers
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
        GIT_TAG vulkan-sdk-1.4.363.0
        GIT_SHALLOW TRUE
    )
    FetchContent_GetProperties(vulkan_headers)
    if(NOT vulkan_headers_POPULATED)
        FetchContent_Populate(vulkan_headers)
    endif()

    # 2. Volk Vulkan Meta-Loader (header-only dynamic dispatch — zero libvulkan link requirement!)
    FetchContent_Declare(
        volk_headers
        GIT_REPOSITORY https://github.com/zeux/volk.git
        GIT_TAG vulkan-sdk-1.4.363.0
        GIT_SHALLOW TRUE
    )
    FetchContent_GetProperties(volk_headers)
    if(NOT volk_headers_POPULATED)
        FetchContent_Populate(volk_headers)
    endif()

    # 3. miniaudio (header-only cross-platform AAudio/OpenSL/WASAPI/ALSA real-time audio synth)
    FetchContent_Declare(
        miniaudio_headers
        GIT_REPOSITORY https://github.com/mackron/miniaudio.git
        GIT_TAG 0.11.26
        GIT_SHALLOW TRUE
    )
    FetchContent_GetProperties(miniaudio_headers)
    if(NOT miniaudio_headers_POPULATED)
        FetchContent_Populate(miniaudio_headers)
    endif()

    # 4. stb (header-only stb_truetype Korean font atlas & stb_image_write PNG screenshot)
    FetchContent_Declare(
        stb_headers
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG 6b5799bae8a1fe3792691ef4b72ae26434c3e799
    )
    FetchContent_GetProperties(stb_headers)
    if(NOT stb_headers_POPULATED)
        FetchContent_Populate(stb_headers)
    endif()

    if(NOT ANDROID)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            glfw
            GIT_REPOSITORY https://github.com/glfw/glfw.git
            GIT_TAG 3.4
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(glfw)
    endif()

    set(OPEN_CITY_APP_SOURCES
        src/main.cpp
        src/platform/PlatformApp.cpp
        src/audio/SynthAudio.cpp
        src/render/VulkanContext.cpp
        src/render/CarMeshGenerator.cpp
        src/render/Renderer3D.cpp
        src/ui/Vulkan2D.cpp
        src/ui/HudRenderer.cpp
        src/ui/TouchControls.cpp
    )

    if(ANDROID)
        set(ANDROID_GLUE_DIR "${ANDROID_NDK}/sources/android/native_app_glue")
        set(ANDROID_GLUE_SRC "${ANDROID_GLUE_DIR}/android_native_app_glue.c")
        set_source_files_properties("${ANDROID_GLUE_SRC}" PROPERTIES
            COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-sign-compare"
        )
        add_library(open_city SHARED ${OPEN_CITY_APP_SOURCES} "${ANDROID_GLUE_SRC}")
        target_include_directories(open_city SYSTEM PRIVATE
            "${vulkan_headers_SOURCE_DIR}/include"
            "${volk_headers_SOURCE_DIR}"
            "${miniaudio_headers_SOURCE_DIR}"
            "${stb_headers_SOURCE_DIR}"
            "${ANDROID_GLUE_DIR}"
        )
        target_compile_definitions(open_city PRIVATE VK_NO_PROTOTYPES VK_USE_PLATFORM_ANDROID_KHR)
        target_link_libraries(open_city PRIVATE open_city_core log android dl OpenSLES)
        target_link_options(open_city PRIVATE "-u" "ANativeActivity_onCreate")
        if(ANDROID_ABI STREQUAL "arm64-v8a" OR ANDROID_ABI STREQUAL "x86_64")
            target_link_options(open_city PRIVATE "-Wl,-z,max-page-size=16384")
        endif()
        target_compile_options(open_city PRIVATE -Wall -Wextra -Wpedantic)
    else()
        add_executable(open_city ${OPEN_CITY_APP_SOURCES})
        target_include_directories(open_city SYSTEM PRIVATE
            "${vulkan_headers_SOURCE_DIR}/include"
            "${volk_headers_SOURCE_DIR}"
            "${miniaudio_headers_SOURCE_DIR}"
            "${stb_headers_SOURCE_DIR}"
        )
        target_compile_definitions(open_city PRIVATE VK_NO_PROTOTYPES)
        target_link_libraries(open_city PRIVATE open_city_core glfw ${CMAKE_DL_LIBS})

        if(WIN32)
            target_link_libraries(open_city PRIVATE winmm ole32)
        elseif(UNIX)
            find_package(Threads REQUIRED)
            target_link_libraries(open_city PRIVATE Threads::Threads m)
        endif()

        if(MSVC)
            target_compile_options(open_city PRIVATE /utf-8 /W4 /permissive-)
            target_link_options(open_city PRIVATE "/SUBSYSTEM:WINDOWS" "/ENTRY:mainCRTStartup")
        elseif(MINGW)
            target_compile_options(open_city PRIVATE -Wall -Wextra -Wpedantic)
            target_link_options(open_city PRIVATE -mwindows -static -static-libgcc -static-libstdc++)
        else()
            target_compile_options(open_city PRIVATE -Wall -Wextra -Wpedantic)
        endif()

        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
            add_custom_command(TARGET open_city POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
                    "$<TARGET_FILE_DIR:open_city>/assets"
                COMMENT "Staging real Seoul GIS & PBR assets next to open_city binary"
            )
        endif()
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
