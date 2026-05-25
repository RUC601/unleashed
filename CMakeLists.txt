cmake_minimum_required(VERSION 3.25)

project(Unleashed LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# ImGui source files
# ============================================================================

set(IMGUI_SOURCES
    vendor/imgui/imgui.cpp
    vendor/imgui/imgui_draw.cpp
    vendor/imgui/imgui_tables.cpp
    vendor/imgui/imgui_widgets.cpp
    vendor/imgui/backends/imgui_impl_win32.cpp
    vendor/imgui/backends/imgui_impl_dx11.cpp
)

# ============================================================================
# Game / Engine source files
# ============================================================================

set(GAME_HEADERS
    include/resource.h
    include/Game/Decrypt.hpp
    include/Game/Entity.hpp
    include/Game/Offsets.hpp
    include/Game/Overwatch.hpp
    include/Game/SDK.hpp
    include/Game/Structs.hpp
    include/Game/Target.hpp
    include/Kmbox/KmboxB.h
    include/Kmbox/KmBoxConfig.h
    include/Kmbox/KmBoxData.h
    include/Kmbox/KmBoxNetManager.h
    include/Memory/Memory.h
    include/Memory/MemoryRanges.h
    include/Renderer/IconManager.hpp
    include/Renderer/Overlay.hpp
    include/Renderer/Renderer.hpp
    include/Utils/Config.hpp
    include/Utils/Types.hpp
    include/Features/UI.hpp
)

set(UNLEASHED_SOURCES
    src/main.cpp
    src/unleashed.rc
    src/Memory/Memory.cpp
    src/Renderer/IconManager.cpp
    src/Renderer/Overlay.cpp
    src/Renderer/Renderer.cpp
    src/Utils/Types.cpp
    src/Utils/Config.cpp
    src/Features/UI.cpp
    src/Game/Offsets.cpp
    src/Kmbox/KmboxB.cpp
    src/Kmbox/KmBoxNetManager.cpp
    vendor/stb/stb_image.cpp
)

# ============================================================================
# Executable
# ============================================================================

add_executable(Unleashed
    ${UNLEASHED_SOURCES}
    ${IMGUI_SOURCES}
)

# ============================================================================
# Include directories
# ============================================================================

target_include_directories(Unleashed PRIVATE
    include
    vendor/leechcore
    vendor/imgui
    vendor/imgui/backends
    vendor/stb
)

# ============================================================================
# Link libraries
# ============================================================================

target_link_libraries(Unleashed PRIVATE
    "${CMAKE_SOURCE_DIR}/vendor/leechcore/leechcore.lib"
    "${CMAKE_SOURCE_DIR}/vendor/leechcore/vmm.lib"
    d3d11
    dxgi
    windowscodecs
    ole32
    ws2_32
    dwmapi
    setupapi
)

# ============================================================================
# Post-build: copy DMA runtime DLLs
# ============================================================================

function(copy_leechcore_runtime target_name)
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/vendor/leechcore/leechcore.dll"
            "$<TARGET_FILE_DIR:${target_name}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/vendor/leechcore/vmm.dll"
            "$<TARGET_FILE_DIR:${target_name}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/vendor/leechcore/FTD3XX.dll"
            "$<TARGET_FILE_DIR:${target_name}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/vendor/leechcore/FTD3XXWU.dll"
            "$<TARGET_FILE_DIR:${target_name}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/vendor/leechcore/leechcore_device_rawtcp.dll"
            "$<TARGET_FILE_DIR:${target_name}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/vendor/leechcore/leechcore_device_hvsavedstate.dll"
            "$<TARGET_FILE_DIR:${target_name}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/vendor/leechcore/leechcore_driver.dll"
            "$<TARGET_FILE_DIR:${target_name}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/vendor/leechcore/Helper64.dll"
            "$<TARGET_FILE_DIR:${target_name}>"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${target_name}>/config"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/config/dma_device.cfg"
            "$<TARGET_FILE_DIR:${target_name}>/config/dma_device.cfg"
    )
endfunction()

copy_leechcore_runtime(Unleashed)

add_custom_command(TARGET Unleashed POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/icons"
        "$<TARGET_FILE_DIR:Unleashed>/assets/icons"
)

# ============================================================================
# Compiler options
# ============================================================================

# ============================================================================
# Scanner executable (standalone offset finder)
# ============================================================================

add_executable(Scanner src/scanner.cpp)

target_include_directories(Scanner PRIVATE include vendor/leechcore)

target_link_libraries(Scanner PRIVATE
    "${CMAKE_SOURCE_DIR}/vendor/leechcore/leechcore.lib"
    "${CMAKE_SOURCE_DIR}/vendor/leechcore/vmm.lib"
    ws2_32
)

copy_leechcore_runtime(Scanner)

target_compile_options(Scanner PRIVATE /W3 /wd4244 /wd4267)
target_compile_definitions(Scanner PRIVATE _CRT_SECURE_NO_WARNINGS)


# ============================================================================
# DMA Verify Constants - validate fixed DMA read chains and transform constants
# ============================================================================

add_executable(DmaVerify src/dma_verify_constants.cpp)

target_include_directories(DmaVerify PRIVATE include vendor/leechcore)

target_link_libraries(DmaVerify PRIVATE
    "${CMAKE_SOURCE_DIR}/vendor/leechcore/leechcore.lib"
    "${CMAKE_SOURCE_DIR}/vendor/leechcore/vmm.lib"
    ws2_32
)

copy_leechcore_runtime(DmaVerify)

target_compile_options(DmaVerify PRIVATE /W3 /wd4244 /wd4267 /wd4200)
target_compile_definitions(DmaVerify PRIVATE _CRT_SECURE_NO_WARNINGS)

# ============================================================================
# Final Probe — comprehensive entity/viewmatrix/decrypt scan
# ============================================================================

add_executable(FinalProbe src/final_probe.cpp)

target_include_directories(FinalProbe PRIVATE include vendor/leechcore)

target_link_libraries(FinalProbe PRIVATE
    "${CMAKE_SOURCE_DIR}/vendor/leechcore/leechcore.lib"
    "${CMAKE_SOURCE_DIR}/vendor/leechcore/vmm.lib"
    ws2_32
)

copy_leechcore_runtime(FinalProbe)

target_compile_options(FinalProbe PRIVATE /W3 /wd4244 /wd4267)
target_compile_definitions(FinalProbe PRIVATE _CRT_SECURE_NO_WARNINGS)

# ============================================================================
# DecryptEmu - Unicorn-based decryption algorithm verification
# ============================================================================
add_executable(DecryptEmu src/decrypt_emu.cpp)
target_include_directories(DecryptEmu PRIVATE include)
target_compile_options(DecryptEmu PRIVATE /W3 /wd4244 /wd4267)
target_compile_definitions(DecryptEmu PRIVATE _CRT_SECURE_NO_WARNINGS)
add_custom_command(TARGET DecryptEmu POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/vendor/unicorn/unicorn.dll"
        $<TARGET_FILE_DIR:DecryptEmu>)

target_compile_options(Unleashed PRIVATE /W3 /wd4244 /wd4267 /wd4305 /wd4200)
target_compile_definitions(Unleashed PRIVATE _CRT_SECURE_NO_WARNINGS)

# ============================================================================
# Hero Probe — read currently selected hero from live OW process
# ============================================================================

add_executable(HeroProbe src/hero_probe.cpp)
target_include_directories(HeroProbe PRIVATE include vendor/leechcore)
target_link_libraries(HeroProbe PRIVATE "${CMAKE_SOURCE_DIR}/vendor/leechcore/leechcore.lib" "${CMAKE_SOURCE_DIR}/vendor/leechcore/vmm.lib" ws2_32)
copy_leechcore_runtime(HeroProbe)
target_compile_options(HeroProbe PRIVATE /W3 /wd4244 /wd4267)
target_compile_definitions(HeroProbe PRIVATE _CRT_SECURE_NO_WARNINGS)

# ============================================================================
# KeyScan — complete GetGlobalKey emulation + key resolution
# ============================================================================

add_executable(KeyScan src/keyscan.cpp)
target_include_directories(KeyScan PRIVATE vendor/leechcore)
target_link_libraries(KeyScan PRIVATE
    "${CMAKE_SOURCE_DIR}/vendor/leechcore/leechcore.lib"
    "${CMAKE_SOURCE_DIR}/vendor/leechcore/vmm.lib"
    ws2_32
)
copy_leechcore_runtime(KeyScan)
target_compile_options(KeyScan PRIVATE /W3 /wd4244 /wd4267)
target_compile_definitions(KeyScan PRIVATE _CRT_SECURE_NO_WARNINGS)

# ============================================================================
# DecryptDump2 — find DecryptComponent + test Key2 candidates (v2, MSVC-safe)
# ============================================================================

add_executable(DecryptDump src/decryptdump2.cpp)
target_include_directories(DecryptDump PRIVATE vendor/leechcore)
target_link_libraries(DecryptDump PRIVATE
    "${CMAKE_SOURCE_DIR}/vendor/leechcore/leechcore.lib"
    "${CMAKE_SOURCE_DIR}/vendor/leechcore/vmm.lib"
    ws2_32
)
copy_leechcore_runtime(DecryptDump)
target_compile_options(DecryptDump PRIVATE /W3 /wd4244 /wd4267)
target_compile_definitions(DecryptDump PRIVATE _CRT_SECURE_NO_WARNINGS)
