# Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
cmake_minimum_required(VERSION 3.24)
project(SkateVLegacy LANGUAGES CXX)

option(SK8V_ENHANCED "Build the isolated Enhanced port" ON)
option(SK8V_ENHANCED_RESOURCE_PROBE "Run the bounded Enhanced resource loading probe" OFF)
option(SK8V_ENHANCED_GRAPHICS_PROBE "Run the bounded Enhanced D3D12 graphics probe" OFF)
option(SK8V_ENHANCED_AUDIO_PROBE "Run the bounded Enhanced native audio probe" OFF)
option(SK8V_ENHANCED_GAMEPLAY "Enable the explicit Enhanced gameplay test build" ON)
set(SK8V_PROBE_COUNT 0)
foreach(probe RESOURCE GRAPHICS AUDIO)
  if(SK8V_ENHANCED_${probe}_PROBE)
    math(EXPR SK8V_PROBE_COUNT "${SK8V_PROBE_COUNT}+1")
  endif()
endforeach()
if(SK8V_PROBE_COUNT GREATER 1 OR (SK8V_PROBE_COUNT GREATER 0 AND NOT SK8V_ENHANCED))
  message(FATAL_ERROR "Choose one isolated Enhanced probe")
endif()
if(SK8V_ENHANCED_GAMEPLAY AND (NOT SK8V_ENHANCED OR SK8V_PROBE_COUNT GREATER 0))
  message(FATAL_ERROR "Enhanced gameplay cannot be mixed with a diagnostic probe")
endif()
if(SK8V_ENHANCED)
  set(SK8V_HOST SkateVEnhanced)
else()
  set(SK8V_HOST SkateVLegacy)
endif()

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "SkateV Legacy host is x64 Windows-only")
endif()

set(SHVDIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/scripthookv" CACHE PATH "Fetched ScriptHookV SDK root")
if(NOT EXISTS "${SHVDIR}/inc/main.h" OR NOT EXISTS "${SHVDIR}/lib/ScriptHookV.lib")
  message(FATAL_ERROR "ScriptHookV SDK not found. Run tools/fetch-scripthookv.ps1 first.")
endif()

add_library(${SK8V_HOST} SHARED
  src/main.cpp
  src/lifecycle.cpp
  src/runtime_bridge.cpp
  src/dynamic_world.cpp
  src/map_states.cpp
  src/ped_bodies.cpp
  src/prop_collision.cpp
  src/game_probe.cpp
  src/ped_collision_native.cpp
  src/ped_collider_builder.cpp
  src/pose_hook.cpp
  src/gta_audio.cpp
  src/audio_entity_hook.cpp
  src/gta_audio_re.cpp
  src/gta_audio_dsp.cpp
  src/live_clip.cpp
  src/hud_overlay.cpp
  src/hud_renderer.cpp
  src/xray_renderer.cpp
  src/records_ui.cpp
  src/board_native.cpp
  src/stall_sampler.cpp
  src/physics_level.cpp
  src/menu.cpp
  src/ps_pad.cpp
  src/board_stencil.cpp
  src/board_tracks.cpp
  src/thread_census.cpp
)
target_compile_features(${SK8V_HOST} PRIVATE cxx_std_20)
target_compile_definitions(${SK8V_HOST} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
if(SK8V_ENHANCED)
  target_sources(${SK8V_HOST} PRIVATE src/gta_audio_enhanced_re.cpp)
  target_sources(${SK8V_HOST} PRIVATE src/enhanced_probe.cpp)
  target_sources(${SK8V_HOST} PRIVATE src/hud_d3d12.cpp)
  target_sources(${SK8V_HOST} PRIVATE src/enhanced_render_dispatch.cpp)
  target_compile_definitions(${SK8V_HOST} PRIVATE SK8V_ENHANCED=1)
  if(NOT SK8V_ENHANCED_GAMEPLAY)
    target_compile_definitions(${SK8V_HOST} PRIVATE SK8V_ENHANCED_PORT_PROBE=1)
  endif()
  if(SK8V_ENHANCED_RESOURCE_PROBE)
    target_compile_definitions(${SK8V_HOST} PRIVATE SK8V_ENHANCED_RESOURCE_PROBE=1)
  endif()
  if(SK8V_ENHANCED_GRAPHICS_PROBE)
    target_sources(${SK8V_HOST} PRIVATE src/enhanced_graphics_probe.cpp)
    target_compile_definitions(${SK8V_HOST} PRIVATE SK8V_ENHANCED_GRAPHICS_PROBE=1)
  endif()
  if(SK8V_ENHANCED_AUDIO_PROBE)
    target_sources(${SK8V_HOST} PRIVATE src/enhanced_audio_probe.cpp)
    target_compile_definitions(${SK8V_HOST} PRIVATE SK8V_ENHANCED_AUDIO_PROBE=1)
  endif()
  target_link_libraries(${SK8V_HOST} PRIVATE d3d12)
  add_library(SK8VHudBridge SHARED addon/sk8v_hud_bridge.cpp)
  target_compile_features(SK8VHudBridge PRIVATE cxx_std_20)
  target_compile_definitions(SK8VHudBridge PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(SK8VHudBridge PRIVATE ../third_party/reshade)
  set_target_properties(SK8VHudBridge PROPERTIES PREFIX "" SUFFIX ".addon64")
endif()

if(SK8V_ENHANCED_AUDIO_PROBE OR SK8V_ENHANCED_GAMEPLAY)
  add_executable(EnhancedAudioTests tests/audio_ring_tests.cpp src/gta_audio_dsp.cpp src/gta_audio_enhanced_re.cpp src/gta_audio_re.cpp)
  target_compile_features(EnhancedAudioTests PRIVATE cxx_std_20)
  target_compile_definitions(EnhancedAudioTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX SK8V_ENHANCED=1)
  target_include_directories(EnhancedAudioTests PRIVATE src)
  if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../port-evidence/live/GTA5_Enhanced.mapped.bin")
    add_test(NAME EnhancedAudioTests COMMAND EnhancedAudioTests "${CMAKE_CURRENT_LIST_DIR}/../port-evidence/live/GTA5_Enhanced.mapped.bin")
  else()
    add_test(NAME EnhancedAudioTests COMMAND EnhancedAudioTests)
  endif()
  add_executable(LegacyAudioRingTests tests/audio_ring_tests.cpp src/gta_audio_dsp.cpp)
  target_compile_features(LegacyAudioRingTests PRIVATE cxx_std_20)
  target_compile_definitions(LegacyAudioRingTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(LegacyAudioRingTests PRIVATE src)
  add_test(NAME LegacyAudioRingTests COMMAND LegacyAudioRingTests)
endif()
target_include_directories(${SK8V_HOST} PRIVATE include "${SHVDIR}/inc")
target_link_libraries(${SK8V_HOST} PRIVATE "${SHVDIR}/lib/ScriptHookV.lib" version xinput hid setupapi shell32)
# Original Skate 3 HUD overlay (hud_overlay.cpp): D3D11 + runtime shader compile.
target_link_libraries(${SK8V_HOST} PRIVATE d3d11 dxgi d3dcompiler)
set_target_properties(${SK8V_HOST} PROPERTIES PREFIX "" SUFFIX ".asi")

if(MSVC)
  target_compile_options(${SK8V_HOST} PRIVATE /W4 /permissive- /EHsc)
  target_compile_options(${SK8V_HOST} PRIVATE $<$<CONFIG:Release>:/O2>)
  # Keep crash symbols locally; embed only the PDB filename.
  target_compile_options(${SK8V_HOST} PRIVATE /Zi)
  target_link_options(${SK8V_HOST} PRIVATE /DEBUG /OPT:REF /OPT:ICF /PDBALTPATH:%_PDB%)
endif()

add_executable(GameBuildTests tests/game_build_tests.cpp)
target_compile_features(GameBuildTests PRIVATE cxx_std_20)
enable_testing()
add_test(NAME GameBuildTests COMMAND GameBuildTests)
add_executable(AnimationDetourTests tests/animation_detour_tests.cpp)
target_compile_features(AnimationDetourTests PRIVATE cxx_std_20)
target_compile_definitions(AnimationDetourTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
add_test(NAME AnimationDetourTests COMMAND AnimationDetourTests)
if(SK8V_ENHANCED)
  # No D3D11 or ScriptHook linkage: the Legacy detour cannot leak into Enhanced.
  add_executable(EnhancedStencilGuardTests tests/enhanced_stencil_guard_tests.cpp src/board_stencil.cpp)
  target_compile_features(EnhancedStencilGuardTests PRIVATE cxx_std_20)
  target_compile_definitions(EnhancedStencilGuardTests PRIVATE SK8V_ENHANCED=1)
  target_include_directories(EnhancedStencilGuardTests PRIVATE src)
  add_test(NAME EnhancedStencilGuardTests COMMAND EnhancedStencilGuardTests)
endif()
add_executable(HudPresentTests tests/hud_present_tests.cpp src/hud_renderer.cpp)
target_compile_features(HudPresentTests PRIVATE cxx_std_20)
target_compile_definitions(HudPresentTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(HudPresentTests PRIVATE src include "${SHVDIR}/inc")
target_link_libraries(HudPresentTests PRIVATE d3d11 dxgi d3dcompiler)
add_test(NAME HudPresentTests COMMAND HudPresentTests)
add_executable(HudD3D12Tests tests/hud_d3d12_tests.cpp src/hud_d3d12.cpp src/hud_renderer.cpp src/xray_renderer.cpp)
target_compile_features(HudD3D12Tests PRIVATE cxx_std_20)
target_compile_definitions(HudD3D12Tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(HudD3D12Tests PRIVATE src include "${SHVDIR}/inc")
target_link_libraries(HudD3D12Tests PRIVATE d3d11 d3d12 dxgi d3dcompiler)
add_test(NAME HudD3D12Tests COMMAND HudD3D12Tests)
if(SK8V_ENHANCED AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/../local/graphics-runtime/ReShade64.dll")
  set(HUD_INTEGRATION_DIR "${CMAKE_CURRENT_BINARY_DIR}/addon-integration")
  add_library(HudAddonTestHost SHARED tests/hud_addon_test_host.cpp src/hud_d3d12.cpp src/hud_renderer.cpp src/xray_renderer.cpp)
  target_compile_features(HudAddonTestHost PRIVATE cxx_std_20)
  target_compile_definitions(HudAddonTestHost PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(HudAddonTestHost PRIVATE src include "${SHVDIR}/inc")
  target_link_libraries(HudAddonTestHost PRIVATE d3d11 d3d12 dxgi d3dcompiler)
  set_target_properties(HudAddonTestHost PROPERTIES PREFIX "" OUTPUT_NAME "SkateVEnhanced" SUFFIX ".asi" RUNTIME_OUTPUT_DIRECTORY "${HUD_INTEGRATION_DIR}" ARCHIVE_OUTPUT_DIRECTORY "${HUD_INTEGRATION_DIR}")
  add_executable(HudAddonIntegrationTests tests/hud_addon_integration_tests.cpp)
  target_compile_features(HudAddonIntegrationTests PRIVATE cxx_std_20)
  target_compile_definitions(HudAddonIntegrationTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(HudAddonIntegrationTests PRIVATE d3d12 dxgi)
  set_target_properties(HudAddonIntegrationTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${HUD_INTEGRATION_DIR}")
  add_dependencies(HudAddonIntegrationTests HudAddonTestHost SK8VHudBridge)
  add_custom_command(TARGET HudAddonIntegrationTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:SK8VHudBridge>" "${HUD_INTEGRATION_DIR}/SK8VHudBridge.addon64"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_LIST_DIR}/../local/graphics-runtime/ReShade64.dll" "${HUD_INTEGRATION_DIR}/ReShade64.dll"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_LIST_DIR}/../tools/SkateVEnhanced.ReShade.ini" "${HUD_INTEGRATION_DIR}/ReShade.ini")
  # Run manually: this reproduces actual injected API hooks on local hardware,
  # separately from the fast WARP tests and without starting GTA.
endif()
if(SK8V_ENHANCED AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/../port-evidence/ped-enhanced/franklin-capture.json")
  file(READ "${CMAKE_CURRENT_LIST_DIR}/../port-evidence/ped-enhanced/franklin-capture.json" SK8V_PED_CAPTURE)
  string(JSON SK8V_PED_PARENT GET "${SK8V_PED_CAPTURE}" parent)
  string(JSON SK8V_PED_OBJECTS GET "${SK8V_PED_CAPTURE}" objects)
  string(JSON SK8V_PED_COUNT GET "${SK8V_PED_CAPTURE}" bone_count)
  add_executable(EnhancedPedBodiesTests tests/ped_body_capture_tests.cpp src/ped_bodies.cpp src/ped_collider_builder.cpp)
  target_compile_features(EnhancedPedBodiesTests PRIVATE cxx_std_20)
  target_compile_definitions(EnhancedPedBodiesTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX SK8V_ENHANCED=1 SK8V_PED_BODIES_READONLY_TEST=1)
  target_include_directories(EnhancedPedBodiesTests PRIVATE src include)
  add_test(NAME EnhancedPedBodiesTests COMMAND EnhancedPedBodiesTests
    "${CMAKE_CURRENT_LIST_DIR}/../port-evidence/ped-enhanced/franklin-memory.bin" "${SK8V_PED_PARENT}" "${SK8V_PED_OBJECTS}" "${SK8V_PED_COUNT}")
endif()

if(SK8V_ENHANCED)
  # Manual, slower integration with the owner's converted assets; no GTA process.
  add_executable(EnhancedRuntimeSmokeTests tests/runtime_smoke_tests.cpp src/runtime_bridge.cpp src/hud_renderer.cpp)
  target_compile_features(EnhancedRuntimeSmokeTests PRIVATE cxx_std_20)
  target_compile_definitions(EnhancedRuntimeSmokeTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX SK8V_ENHANCED=1)
  target_include_directories(EnhancedRuntimeSmokeTests PRIVATE src include)
  target_link_libraries(EnhancedRuntimeSmokeTests PRIVATE d3d11 dxgi d3dcompiler)
endif()
