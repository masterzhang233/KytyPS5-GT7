cmake_minimum_required(VERSION 3.19)
project(KytyDlssBackend LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Backend requires Windows x64 with MSVC or clang-cl")
endif()
set(KYTY_NGX_SDK_ROOT "" CACHE PATH "External NVIDIA DLSS SDK checkout")
if(NOT EXISTS "${KYTY_NGX_SDK_ROOT}/include/nvsdk_ngx_vk.h" OR
   NOT EXISTS "${KYTY_NGX_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib")
    message(FATAL_ERROR "Set KYTY_NGX_SDK_ROOT to the external SDK; no SDK files are vendored")
endif()
add_library(kyty_dlss_backend SHARED backend.cpp)
target_compile_features(kyty_dlss_backend PRIVATE cxx_std_20)
set_property(TARGET kyty_dlss_backend PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_compile_definitions(kyty_dlss_backend PRIVATE VK_NO_PROTOTYPES NOMINMAX WIN32_LEAN_AND_MEAN KYTY_DLSS_BACKEND_BUILD)
target_compile_options(kyty_dlss_backend PRIVATE /W4 /utf-8 /EHsc)
target_include_directories(kyty_dlss_backend SYSTEM PRIVATE "${KYTY_NGX_SDK_ROOT}/include" "${CMAKE_CURRENT_SOURCE_DIR}/../../3rdparty/Vulkan-Headers/include")
target_link_libraries(kyty_dlss_backend PRIVATE
    "$<IF:$<CONFIG:Debug>,${KYTY_NGX_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s_dbg.lib,${KYTY_NGX_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib>"
    advapi32 shell32 shlwapi ole32 crypt32 wintrust version)

add_executable(kyty_dlss_backend_smoke smoke.cpp)
target_compile_features(kyty_dlss_backend_smoke PRIVATE cxx_std_20)
set_property(TARGET kyty_dlss_backend_smoke PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
target_compile_definitions(kyty_dlss_backend_smoke PRIVATE VK_NO_PROTOTYPES NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(kyty_dlss_backend_smoke PRIVATE /W4 /utf-8 /EHsc)
target_include_directories(kyty_dlss_backend_smoke SYSTEM PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../3rdparty/Vulkan-Headers/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../3rdparty/nlohmann_json/single_include")

add_executable(kyty_dlss_device_probe device_probe.cpp)
target_compile_features(kyty_dlss_device_probe PRIVATE cxx_std_20)
set_property(TARGET kyty_dlss_device_probe PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
target_compile_definitions(kyty_dlss_device_probe PRIVATE VK_NO_PROTOTYPES NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(kyty_dlss_device_probe PRIVATE /W4 /utf-8 /EHsc)
target_include_directories(kyty_dlss_device_probe SYSTEM PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../3rdparty/Vulkan-Headers/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../3rdparty/nlohmann_json/single_include")
