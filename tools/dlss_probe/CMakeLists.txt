cmake_minimum_required(VERSION 3.19)
project(KytyDlssProbe LANGUAGES CXX)

# This is an independent diagnostic target. No NGX SDK files are downloaded or
# copied into the emulator repository or its normal release packages.
if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "The DLSS probe currently requires Windows with MSVC or clang-cl")
endif()
set(KYTY_NGX_SDK_ROOT "" CACHE PATH "External NVIDIA DLSS SDK checkout")
if(NOT EXISTS "${KYTY_NGX_SDK_ROOT}/include/nvsdk_ngx_vk.h" OR
   NOT EXISTS "${KYTY_NGX_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib")
    message(FATAL_ERROR "Set KYTY_NGX_SDK_ROOT to an external NVIDIA DLSS SDK checkout")
endif()
add_executable(kyty_dlss_probe main.cpp)
target_compile_features(kyty_dlss_probe PRIVATE cxx_std_20)
set_property(TARGET kyty_dlss_probe PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_compile_definitions(kyty_dlss_probe PRIVATE VK_NO_PROTOTYPES NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(kyty_dlss_probe PRIVATE /W4 /utf-8 /EHsc)
target_include_directories(kyty_dlss_probe SYSTEM PRIVATE
    "${KYTY_NGX_SDK_ROOT}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../3rdparty/Vulkan-Headers/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../3rdparty/nlohmann_json/single_include")
target_link_libraries(kyty_dlss_probe PRIVATE
    "$<IF:$<CONFIG:Debug>,${KYTY_NGX_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s_dbg.lib,${KYTY_NGX_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib>"
    advapi32 shell32 shlwapi ole32 crypt32 wintrust version)
