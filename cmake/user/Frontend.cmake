function(user_configure_frontend project_root binary_dir)
    set(embed_script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/generate_embed.py")
    set(embed_header "${binary_dir}/embed.h")
    set(bootstrap_lua "${project_root}/project/src/lua/bootstrap.lua")
    set(sb_profile "${project_root}/project/resources/permissions.sb")

    add_custom_target(user_vite_build ALL)

    add_custom_target(user_generate_embed ALL
        COMMAND "${CMAKE_COMMAND}" -E env python3 "${embed_script}"
            "${embed_header}"
            "${bootstrap_lua}" BOOTSTRAP_LUA
            "${sb_profile}" SB_PROFILE
        BYPRODUCTS "${embed_header}"
        DEPENDS user_vite_build
        COMMENT "Generating embed.h for user build"
        VERBATIM
    )
endfunction()

function(user_attach_frontend_to_target target_name project_root binary_dir)
    set(bundle_resources_dir "$<TARGET_BUNDLE_DIR:${target_name}>/Contents/Resources")
    set(prebundled_main "${project_root}/project/prebundled-ui/main")
    set(prebundled_overlay "${project_root}/project/prebundled-ui/overlay")

    if(NOT EXISTS "${prebundled_main}/main.html")
        message(FATAL_ERROR "Missing prebundled main UI: ${prebundled_main}/main.html")
    endif()
    if(NOT EXISTS "${prebundled_overlay}/macro_controls.html")
        message(FATAL_ERROR "Missing prebundled overlay UI: ${prebundled_overlay}/macro_controls.html")
    endif()

    add_custom_command(TARGET "${target_name}" POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${bundle_resources_dir}/main"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${bundle_resources_dir}/overlay"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${prebundled_main}" "${bundle_resources_dir}/main"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${prebundled_overlay}" "${bundle_resources_dir}/overlay"
        COMMENT "Copying prebundled frontend assets into app bundle"
        VERBATIM
    )
endfunction()
