set(USER_IMGUI_INCLUDE_DIR "${PROJECT_ROOT}/project/libs/imgui")
set(USER_IMGUI_BACKENDS_INCLUDE_DIR "${PROJECT_ROOT}/project/libs/imgui/backends")

set(USER_IMGUI_SOURCES
    "${USER_IMGUI_INCLUDE_DIR}/imgui.cpp"
    "${USER_IMGUI_INCLUDE_DIR}/imgui_draw.cpp"
    "${USER_IMGUI_INCLUDE_DIR}/imgui_widgets.cpp"
    "${USER_IMGUI_INCLUDE_DIR}/imgui_tables.cpp"
    "${USER_IMGUI_INCLUDE_DIR}/imgui_demo.cpp"
    "${PROJECT_ROOT}/project/src/macos/imgui_impl_osx.mm"
    "${PROJECT_ROOT}/project/src/macos/imgui_impl_metal.mm"
    "${PROJECT_ROOT}/project/src/macos/overlay_provider.mm"
)

set(USER_MACOS_SOURCES
    "${PROJECT_ROOT}/project/src/macos/macos_input.cpp"
    "${PROJECT_ROOT}/project/src/macos/macos_utils.cpp"
    "${PROJECT_ROOT}/project/src/macos/screencapture.mm"
    "${PROJECT_ROOT}/project/src/macos/ocr.mm"
    "${PROJECT_ROOT}/project/src/macos/img_utils.cpp"
    "${PROJECT_ROOT}/project/src/macos/webview.mm"
    "${PROJECT_ROOT}/project/src/macos/objc_utils.mm"
    "${PROJECT_ROOT}/project/src/macos/lua_ls.cpp"
    "${PROJECT_ROOT}/project/src/macos/dialog.mm"
)
