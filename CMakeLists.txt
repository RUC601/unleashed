cmake_minimum_required(VERSION 3.25)

project(Unleashed LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(UNLEASHED_TEST_SERVER "Build the optional localhost read-only test server" ON)

enable_testing()

if(MSVC)
    add_compile_options(/utf-8)
endif()

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
    include/Game/AbilityIcons.hpp
    include/Game/AimArchitecture.hpp
    include/Game/Decrypt.hpp
    include/Game/Entity.hpp
    include/Game/HeroGeometrySpec.hpp
    include/Game/HeroPerkLookupSeed.hpp
    include/Game/HeroPerkRuntime.hpp
    include/Game/HeroPerks.hpp
    include/Game/HeroSkills.hpp
    include/Game/InputOrchestrator.hpp
    include/Game/LeadPrediction.hpp
    include/Game/Offsets.hpp
    include/Game/Overwatch.hpp
    include/Game/OutputOwnership.hpp
    include/Game/SDK.hpp
    include/Game/Structs.hpp
    include/Game/Target.hpp
    include/Game/WeaponSpec.hpp
    include/Kmbox/KmboxB.h
    include/Kmbox/KmboxMoveTest.h
    include/Kmbox/KmboxRuntime.hpp
    include/Kmbox/KmboxTimerResolution.h
    include/Kmbox/KmBoxConfig.h
    include/Kmbox/KmBoxData.h
    include/Kmbox/KmBoxMock.h
    include/Kmbox/KmBoxNetManager.h
    include/Memory/KeyState.hpp
    include/Memory/Memory.h
    include/Memory/MemoryRanges.h
    include/Renderer/IconManager.hpp
    include/Renderer/Overlay.hpp
    include/Renderer/Renderer.hpp
    include/Utils/Config.hpp
    include/Utils/CrashDump.hpp
    include/Utils/Diagnostics.hpp
    include/Utils/InputLabels.hpp
    include/Utils/TestServer.hpp
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
    src/Utils/CrashDump.cpp
    src/Utils/Diagnostics.cpp
    src/Utils/TestServer.cpp
    src/Utils/Types.cpp
    src/Utils/Config.cpp
    src/Features/UI.cpp
    src/Game/HeroGeometrySpec.cpp
    src/Game/HeroPerkRuntime.cpp
    src/Game/HeroSkills.cpp
    src/Game/Offsets.cpp
    src/Game/OutputOwnership.cpp
    src/Game/WeaponSpec.cpp
    src/Kmbox/KmboxB.cpp
    src/Kmbox/KmboxMoveTest.cpp
    src/Kmbox/KmboxRuntime.cpp
    src/Kmbox/KmboxTimerResolution.cpp
    src/Kmbox/KmBoxMock.cpp
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

add_executable(MotionEstimatorSelfTest
    src/Tools/MotionEstimatorSelfTest.cpp
)

add_executable(FovConfigSelfTest
    src/Tools/FovConfigSelfTest.cpp
)

add_executable(TrackingDeadzoneDampingSelfTest
    src/Tools/TrackingDeadzoneDampingSelfTest.cpp
)

add_executable(AimPresetConfigSelfTest
    src/Tools/AimPresetConfigSelfTest.cpp
)

add_executable(LeadPredictionSelfTest
    src/Tools/LeadPredictionSelfTest.cpp
)

add_executable(KeyStateSelfTest
    src/Tools/KeyStateSelfTest.cpp
)

add_executable(WeaponSpecDisplaySelfTest
    src/Tools/WeaponSpecDisplaySelfTest.cpp
    src/Game/HeroPerkRuntime.cpp
    src/Game/WeaponSpec.cpp
    src/Utils/Diagnostics.cpp
)

add_executable(HeroGeometrySpecSelfTest
    src/Tools/HeroGeometrySpecSelfTest.cpp
    src/Game/HeroGeometrySpec.cpp
)

add_executable(HeroSkillConfigSelfTest
    src/Tools/HeroSkillConfigSelfTest.cpp
)

add_executable(HeroPerkClassifierSelfTest
    src/Tools/HeroPerkClassifierSelfTest.cpp
)

add_executable(MockHardwareSelfTest
    src/Tools/MockHardwareSelfTest.cpp
    src/Kmbox/KmboxRuntime.cpp
    src/Kmbox/KmboxTimerResolution.cpp
    src/Kmbox/KmBoxMock.cpp
    src/Kmbox/KmBoxNetManager.cpp
    src/Kmbox/KmboxB.cpp
    src/Utils/Diagnostics.cpp
)

add_executable(KmboxQueueSelfTest
    src/Tools/KmboxQueueSelfTest.cpp
    src/Kmbox/KmBoxMock.cpp
    src/Kmbox/KmBoxNetManager.cpp
    src/Kmbox/KmboxB.cpp
    src/Utils/Diagnostics.cpp
)

add_executable(KmboxLifecycleSelfTest
    src/Tools/KmboxLifecycleSelfTest.cpp
    src/Kmbox/KmBoxMock.cpp
    src/Kmbox/KmBoxNetManager.cpp
    src/Kmbox/KmboxB.cpp
    src/Utils/Diagnostics.cpp
)

add_executable(KmboxRuntimeSwitchSelfTest
    src/Tools/KmboxRuntimeSwitchSelfTest.cpp
    src/Kmbox/KmboxRuntime.cpp
    src/Kmbox/KmboxTimerResolution.cpp
    src/Kmbox/KmBoxMock.cpp
    src/Kmbox/KmBoxNetManager.cpp
    src/Kmbox/KmboxB.cpp
    src/Utils/Diagnostics.cpp
)

add_executable(InputOwnershipSelfTest
    src/Tools/InputOwnershipSelfTest.cpp
    src/Game/OutputOwnership.cpp
)

add_test(NAME MotionEstimatorSelfTest COMMAND MotionEstimatorSelfTest)
add_test(NAME FovConfigSelfTest COMMAND FovConfigSelfTest)
add_test(NAME TrackingDeadzoneDampingSelfTest COMMAND TrackingDeadzoneDampingSelfTest)
add_test(NAME AimPresetConfigSelfTest COMMAND AimPresetConfigSelfTest)
add_test(NAME LeadPredictionSelfTest COMMAND LeadPredictionSelfTest)
add_test(NAME KeyStateSelfTest COMMAND KeyStateSelfTest)
add_test(NAME WeaponSpecDisplaySelfTest COMMAND WeaponSpecDisplaySelfTest)
add_test(NAME HeroGeometrySpecSelfTest COMMAND HeroGeometrySpecSelfTest)
add_test(NAME HeroSkillConfigSelfTest COMMAND HeroSkillConfigSelfTest)
add_test(NAME HeroPerkClassifierSelfTest COMMAND HeroPerkClassifierSelfTest)
add_test(NAME MockHardwareSelfTest COMMAND MockHardwareSelfTest)
add_test(NAME KmboxQueueSelfTest COMMAND KmboxQueueSelfTest)
add_test(NAME KmboxLifecycleSelfTest COMMAND KmboxLifecycleSelfTest)
add_test(NAME KmboxRuntimeSwitchSelfTest COMMAND KmboxRuntimeSwitchSelfTest)
add_test(NAME InputOwnershipSelfTest COMMAND InputOwnershipSelfTest)

# ============================================================================
# Include directories
# ============================================================================

target_include_directories(Unleashed PRIVATE
    include
    vendor/leechcore
    vendor/imgui
    vendor/imgui/backends
    vendor/rapidjson
    vendor/stb
)

target_include_directories(MotionEstimatorSelfTest PRIVATE
    include
    vendor/imgui
)

target_include_directories(FovConfigSelfTest PRIVATE
    include
    vendor/imgui
)

target_include_directories(TrackingDeadzoneDampingSelfTest PRIVATE
    include
    vendor/imgui
)

target_include_directories(AimPresetConfigSelfTest PRIVATE
    include
    vendor/imgui
)

target_include_directories(LeadPredictionSelfTest PRIVATE
    include
    vendor/leechcore
    vendor/imgui
)

target_include_directories(KeyStateSelfTest PRIVATE
    include
    vendor/leechcore
    vendor/imgui
)

target_include_directories(WeaponSpecDisplaySelfTest PRIVATE
    include
    vendor/leechcore
    vendor/imgui
)

target_include_directories(HeroGeometrySpecSelfTest PRIVATE
    include
    vendor/leechcore
    vendor/imgui
)

target_include_directories(HeroSkillConfigSelfTest PRIVATE
    include
    vendor/leechcore
    vendor/imgui
)

target_include_directories(HeroPerkClassifierSelfTest PRIVATE
    include
    vendor/leechcore
)

target_include_directories(MockHardwareSelfTest PRIVATE
    include
    vendor/leechcore
    vendor/imgui
)

target_include_directories(KmboxQueueSelfTest PRIVATE
    include
    vendor/leechcore
    vendor/imgui
)

target_include_directories(KmboxLifecycleSelfTest PRIVATE
    include
    vendor/leechcore
    vendor/imgui
)

target_include_directories(KmboxRuntimeSwitchSelfTest PRIVATE
    include
    vendor/leechcore
    vendor/imgui
)

target_include_directories(InputOwnershipSelfTest PRIVATE
    include
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
    winmm
    shell32
    dwmapi
    setupapi
    dbghelp
)

target_link_libraries(MockHardwareSelfTest PRIVATE
    ws2_32
    winmm
    setupapi
)

target_link_libraries(KmboxQueueSelfTest PRIVATE
    ws2_32
    setupapi
)

target_link_libraries(KmboxLifecycleSelfTest PRIVATE
    ws2_32
    setupapi
)

target_link_libraries(KmboxRuntimeSwitchSelfTest PRIVATE
    ws2_32
    winmm
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
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/heroes"
        "$<TARGET_FILE_DIR:Unleashed>/assets/heroes"
)

set(ABILITY_ICON_ASSET_DIR "${CMAKE_SOURCE_DIR}/assets/ability-icons")

if(EXISTS "${ABILITY_ICON_ASSET_DIR}")
    add_custom_command(TARGET Unleashed POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ABILITY_ICON_ASSET_DIR}"
            "$<TARGET_FILE_DIR:Unleashed>/assets/ability-icons"
    )
else()
    message(WARNING "Ability icon assets are missing: ${ABILITY_ICON_ASSET_DIR}")
endif()

# ============================================================================
# Compiler options
# ============================================================================

if(MSVC)
    function(enable_release_symbols target_name)
        target_compile_options(${target_name} PRIVATE "$<$<CONFIG:Release>:/Zi>")
        target_link_options(${target_name} PRIVATE "$<$<CONFIG:Release>:/DEBUG:FULL>")
    endfunction()

    enable_release_symbols(Unleashed)
endif()

target_compile_options(Unleashed PRIVATE /W3 /wd4244 /wd4267 /wd4305 /wd4200)
target_compile_definitions(Unleashed PRIVATE _CRT_SECURE_NO_WARNINGS)
if(UNLEASHED_TEST_SERVER)
    target_compile_definitions(Unleashed PRIVATE UNLEASHED_TEST_SERVER=1)
endif()

target_compile_options(MotionEstimatorSelfTest PRIVATE /W3 /wd4244 /wd4267 /wd4305 /wd4200)
target_compile_definitions(MotionEstimatorSelfTest PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(FovConfigSelfTest PRIVATE /W3 /wd4244 /wd4267 /wd4305 /wd4200)
target_compile_definitions(FovConfigSelfTest PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(TrackingDeadzoneDampingSelfTest PRIVATE /W3 /wd4244 /wd4267 /wd4305 /wd4200)
target_compile_definitions(TrackingDeadzoneDampingSelfTest PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(AimPresetConfigSelfTest PRIVATE /W3 /wd4244 /wd4267 /wd4305 /wd4200)
target_compile_definitions(AimPresetConfigSelfTest PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(LeadPredictionSelfTest PRIVATE /W3 /wd4244 /wd4267 /wd4305 /wd4200)
target_compile_definitions(LeadPredictionSelfTest PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(KeyStateSelfTest PRIVATE /W3 /wd4244 /wd4267 /wd4305 /wd4200)
target_compile_definitions(KeyStateSelfTest PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(WeaponSpecDisplaySelfTest PRIVATE /W3 /wd4244 /wd4267 /wd4305 /wd4200)
target_compile_definitions(WeaponSpecDisplaySelfTest PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(HeroGeometrySpecSelfTest PRIVATE /W3 /wd4244 /wd4267 /wd4305 /wd4200)
target_compile_definitions(HeroGeometrySpecSelfTest PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(HeroSkillConfigSelfTest PRIVATE /W3 /wd4244 /wd4267 /wd4305 /wd4200)
target_compile_definitions(HeroSkillConfigSelfTest PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(MockHardwareSelfTest PRIVATE /W3 /wd4244 /wd4267 /wd4305 /wd4200)
target_compile_definitions(MockHardwareSelfTest PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(KmboxQueueSelfTest PRIVATE /W3 /wd4244 /wd4267 /wd4305 /wd4200)
target_compile_definitions(KmboxQueueSelfTest PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(KmboxLifecycleSelfTest PRIVATE /W3 /wd4244 /wd4267 /wd4305 /wd4200)
target_compile_definitions(KmboxLifecycleSelfTest PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(KmboxRuntimeSwitchSelfTest PRIVATE /W3 /wd4244 /wd4267 /wd4305 /wd4200)
target_compile_definitions(KmboxRuntimeSwitchSelfTest PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(InputOwnershipSelfTest PRIVATE /W3 /wd4244 /wd4267 /wd4305 /wd4200)
target_compile_definitions(InputOwnershipSelfTest PRIVATE _CRT_SECURE_NO_WARNINGS)
