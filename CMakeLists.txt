cmake_minimum_required(VERSION 3.24)
project(SDHDR VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "SD-HDR builds on Windows only.")
endif()

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
if(MSVC)
    target_compile_options(imgui PRIVATE /W0 /EHsc)
endif()

file(GLOB CPPWINRT_CANDIDATES "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/cppwinrt.exe")
if(CPPWINRT_CANDIDATES)
    list(SORT CPPWINRT_CANDIDATES COMPARE NATURAL ORDER DESCENDING)
    list(GET CPPWINRT_CANDIDATES 0 CPPWINRT_EXE)
endif()
if(NOT CPPWINRT_EXE)
    find_program(CPPWINRT_EXE cppwinrt.exe)
endif()
if(NOT CPPWINRT_EXE)
    message(FATAL_ERROR "cppwinrt.exe was not found. Install the Windows 11 SDK.")
endif()

set(WINRT_OUT ${CMAKE_BINARY_DIR}/generated)
set(WINRT_STAMP ${WINRT_OUT}/winrt/base.h)
if(NOT EXISTS ${WINRT_STAMP})
    file(MAKE_DIRECTORY ${WINRT_OUT})
    message(STATUS "Generating C++/WinRT headers with ${CPPWINRT_EXE}")
    execute_process(
        COMMAND "${CPPWINRT_EXE}" -input sdk -output "${WINRT_OUT}"
            -include Windows.Foundation+Windows.Foundation.Metadata+Windows.Graphics.Capture+Windows.Graphics.DirectX+Windows.Graphics.DirectX.Direct3D11+Windows.System
        RESULT_VARIABLE CPPWINRT_RESULT
        OUTPUT_VARIABLE CPPWINRT_OUT
        ERROR_VARIABLE CPPWINRT_ERR
    )
    if(NOT CPPWINRT_RESULT EQUAL 0)
        message(STATUS "Filtered cppwinrt failed, generating the full projection")
        execute_process(
            COMMAND "${CPPWINRT_EXE}" -input sdk -output "${WINRT_OUT}"
            RESULT_VARIABLE CPPWINRT_RESULT
            OUTPUT_VARIABLE CPPWINRT_OUT
            ERROR_VARIABLE CPPWINRT_ERR
        )
    endif()
    if(NOT CPPWINRT_RESULT EQUAL 0)
        message(FATAL_ERROR "cppwinrt failed (${CPPWINRT_RESULT}): ${CPPWINRT_ERR}")
    endif()
endif()

add_executable(SDHDR WIN32
    src/main.cpp
    src/App.cpp
    src/AppRules.cpp
    src/CaptureSession.cpp
    src/MonitorManager.cpp
    src/OverlayWindow.cpp
    src/Renderer.cpp
    src/SettingsUi.cpp
    src/ToneMapParams.cpp
    src/TrayUI.cpp
    app.manifest
)

target_include_directories(SDHDR PRIVATE src ${WINRT_OUT})
target_compile_definitions(SDHDR PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(SDHDR PRIVATE
    imgui
    d3d11
    dxgi
    d3dcompiler
    dxguid
    dwmapi
    windowsapp
    runtimeobject
    shcore
    shell32
    ole32
    user32
    gdi32
    advapi32
    windowscodecs
)

if(MSVC)
    target_compile_options(SDHDR PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8 /EHsc /bigobj /MP)
    target_link_options(SDHDR PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${CMAKE_SOURCE_DIR}/app.manifest")
endif()

add_custom_command(TARGET SDHDR POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:SDHDR>/shaders
)

install(TARGETS SDHDR RUNTIME DESTINATION .)
install(DIRECTORY shaders DESTINATION .)
