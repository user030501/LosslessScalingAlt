cmake_minimum_required(VERSION 3.21)
project(LSAlt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS OFF)

if (MSVC)
    add_compile_options(/W4 /permissive-)
endif()

find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(LSAlt WIN32
    src/main.cpp
    src/App.cpp
    src/core/Logger.cpp
    src/core/ConfigManager.cpp
    src/gpu/GpuDeviceManager.cpp
    src/monitor/PerfMonitor.cpp
    src/profiles/GameProfile.cpp
    src/profiles/ProfileManager.cpp
    src/framegen/ShaderCompiler.cpp
    src/framegen/MotionEstimator.cpp
    src/framegen/FrameInterpolator.cpp
    src/framegen/FramePacing.cpp
    src/framegen/FrameGenEngine.cpp
    src/framegen/TestPatternSource.cpp
    src/framegen/DiagnosticTests.cpp
    src/ui/UIManager.cpp
    src/ui/panels/HomePanel.cpp
    src/ui/panels/ScalingPanel.cpp
    src/ui/panels/FrameGenPanel.cpp
    src/ui/panels/ProfilesPanel.cpp
    src/ui/panels/MonitorPanel.cpp
    src/ui/panels/GpuSettingsPanel.cpp
    src/ui/panels/AdvancedPanel.cpp
    src/ui/panels/AboutPanel.cpp
)

target_include_directories(LSAlt PRIVATE src)

target_link_libraries(LSAlt PRIVATE
    imgui::imgui
    nlohmann_json::nlohmann_json
    d3d11
    dxgi
    dxguid
    d3dcompiler
    pdh
    psapi
    comctl32
    comdlg32
    dwmapi
)

target_compile_definitions(LSAlt PRIVATE
    UNICODE _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

set_target_properties(LSAlt PROPERTIES OUTPUT_NAME "LSAlt")

# Frame Generation Engine HLSL sources are compiled at runtime (see
# ShaderCompiler) so they must sit next to the executable, not be baked in.
add_custom_command(TARGET LSAlt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:LSAlt>/shaders/framegen"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/src/framegen/shaders"
        "$<TARGET_FILE_DIR:LSAlt>/shaders/framegen"
    COMMENT "Copying Frame Generation Engine HLSL shaders next to the executable"
)
