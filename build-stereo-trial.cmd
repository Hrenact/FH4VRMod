@echo off
setlocal
call "%~dp0tools\setup-msvc.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build\stereo-trial mkdir build\stereo-trial
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_camera_pair_recovery.cpp /Fobuild\stereo-trial\test_camera_pair_recovery.obj /Febuild\stereo-trial\test_camera_pair_recovery.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_multicam_probe.cpp /Fobuild\stereo-trial\test_multicam_probe.obj /Febuild\stereo-trial\test_multicam_probe.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_presentation_camera_probe.cpp /Fobuild\stereo-trial\test_presentation_camera_probe.obj /Febuild\stereo-trial\test_presentation_camera_probe.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_menu_camera_probe.cpp /Fobuild\stereo-trial\test_menu_camera_probe.obj /Febuild\stereo-trial\test_menu_camera_probe.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /DFH4VR_UI_ROOT_PROBE src\test_ui_projection_probe.cpp /Fobuild\stereo-trial\test_ui_projection_probe.obj /Febuild\stereo-trial\test_ui_projection_probe.exe user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_ui_root_probe.cpp /Fobuild\stereo-trial\test_ui_root_probe.obj /Febuild\stereo-trial\test_ui_root_probe.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /DFH4VR_UI_MATERIAL_CLIP src\test_ui_material_probe.cpp /Fobuild\stereo-trial\test_ui_material_probe.obj /Febuild\stereo-trial\test_ui_material_probe.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_ui_material_unlit_gate.cpp /Fobuild\stereo-trial\test_ui_material_unlit_gate.obj /Febuild\stereo-trial\test_ui_material_unlit_gate.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_ui_material_clip.cpp /Fobuild\stereo-trial\test_ui_material_clip.obj /Febuild\stereo-trial\test_ui_material_clip.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_ui_map_probe.cpp /Fobuild\stereo-trial\test_ui_map_probe.obj /Febuild\stereo-trial\test_ui_map_probe.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc tools\test_ui_material_clip_math.cpp /Fobuild\stereo-trial\test_ui_material_clip_math.obj /Febuild\stereo-trial\test_ui_material_clip_math.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_ui_text_forwarding.cpp /Fobuild\stereo-trial\test_ui_text_forwarding.obj /Febuild\stereo-trial\test_ui_text_forwarding.exe user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc tools\test_ui_affine_clip.cpp /Fobuild\stereo-trial\test_ui_affine_clip.obj /Febuild\stereo-trial\test_ui_affine_clip.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_vr_projection.cpp /Fobuild\stereo-trial\test_vr_projection.obj /Febuild\stereo-trial\test_vr_projection.exe user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_near_clip.cpp /Fobuild\stereo-trial\test_near_clip.obj /Febuild\stereo-trial\test_near_clip.exe user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_projection_calibration.cpp /Fobuild\stereo-trial\test_projection_calibration.obj /Febuild\stereo-trial\test_projection_calibration.exe user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_fov_hold.cpp /Fobuild\stereo-trial\test_fov_hold.obj /Febuild\stereo-trial\test_fov_hold.exe user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /DFH4VR_WIDE_FOV_TEST src\test_fov_hold.cpp /Fobuild\stereo-trial\test_wide_fov.obj /Febuild\stereo-trial\test_wide_fov.exe user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_fov_probe.cpp /Fobuild\stereo-trial\test_fov_probe.obj /Febuild\stereo-trial\test_fov_probe.exe user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_stereo_head.cpp /Fobuild\stereo-trial\test_stereo_head.obj /Febuild\stereo-trial\test_stereo_head.exe user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_stereo_trial.cpp /Fobuild\stereo-trial\test_stereo_trial.obj /Febuild\stereo-trial\test_stereo_trial.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /c src\verify_dxgi_slots.cpp /Fobuild\stereo-trial\verify_dxgi_slots.obj
if errorlevel 1 exit /b 1
ml64 /nologo /c /Fobuild\stereo-trial\dxgi_forwarders.obj src\dxgi_forwarders.asm
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /c /Ithird_party\openvr\headers /DFH4VR_MULTICAM_VR /DFH4VR_UI_MAP_PROBE /DFH4VR_UI_MATERIAL_CLIP /DFH4VR_UI_MATERIAL_PROBE /DFH4VR_UI_ROOT_PROBE /DFH4VR_UI_AFFINE_CLIP /DFH4VR_UI_PASS_PROBE /DFH4VR_UI_PROJECTION_PROBE /DFH4VR_WIDE_FOV_TEST /DFH4VR_NEAR_CLIP_TEST /DFH4VR_VR_PROJECTION_TEST /DFH4VR_LIVE_SCENE /DFH4VR_FOV_TEST /DFH4VR_FOV_PROBE /DFH4VR_STEREO_HEAD /DFH4VR_STEREO_TRIAL /DFH4VR_FRAME_CAPTURE /DFH4VR_SWAPCHAIN_WRAPPER /DFH4VR_GRAPHICS_PROBE /DFH4VR_CAMERA_PROBE /DFH4VR_EYE_OFFSET /DFH4VR_EYE_POSITION src\graphics_probe.cpp src\vtable_probe.cpp src\dxgi_proxy.cpp /Fobuild\stereo-trial\
if errorlevel 1 exit /b 1
link /nologo /DLL /OUT:build\stereo-trial\dxgi.dll /DEF:src\dxgi_proxy.def build\stereo-trial\dxgi_forwarders.obj build\stereo-trial\graphics_probe.obj build\stereo-trial\dxgi_proxy.obj build\stereo-trial\vtable_probe.obj kernel32.lib user32.lib windowscodecs.lib ole32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_graphics_probe.cpp /Fobuild\stereo-trial\test_graphics_probe.obj /Febuild\stereo-trial\test_graphics_probe.exe d3d12.lib user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_display_modes.cpp /Fobuild\stereo-trial\test_display_modes.obj /Febuild\stereo-trial\test_display_modes.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_ui_layer_capture.cpp /Fobuild\stereo-trial\test_ui_layer_capture.obj /Febuild\stereo-trial\test_ui_layer_capture.exe d3d12.lib user32.lib windowscodecs.lib ole32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_ui_isolated_capture.cpp /Fobuild\stereo-trial\test_ui_isolated_capture.obj /Febuild\stereo-trial\test_ui_isolated_capture.exe d3d12.lib d3dcompiler.lib user32.lib windowscodecs.lib ole32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_frame_capture.cpp /Fobuild\stereo-trial\test_frame_capture.obj /Febuild\stereo-trial\test_frame_capture.exe d3d12.lib windowscodecs.lib ole32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc src\test_eye_offset.cpp /Fobuild\stereo-trial\test_eye_offset.obj /Febuild\stereo-trial\test_eye_offset.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /DFH4VR_EYE_OFFSET /DFH4VR_EYE_POSITION src\test_position_probe.cpp /Fobuild\stereo-trial\test_position_probe.obj /Febuild\stereo-trial\test_position_probe.exe user32.lib
exit /b %errorlevel%











