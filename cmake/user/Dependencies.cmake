function(user_configure_dependencies project_root)
    set(USER_SOL2_INCLUDE_DIR "${project_root}/project/libs/sol2/include" PARENT_SCOPE)
    set(USER_EIGEN_INCLUDE_DIR "${project_root}/project/libs/eigen" PARENT_SCOPE)
    set(USER_LUAJIT_SRC_DIR "${project_root}/project/libs/luajit/src" PARENT_SCOPE)
    set(USER_OPENCV_INCLUDE_DIRS
        "${project_root}/project/libs/opencv/include"
        "${project_root}/project/libs/opencv/modules/core/include"
        "${project_root}/project/libs/opencv/modules/imgproc/include"
        "${project_root}/project/libs/opencv/modules/imgcodecs/include"
        "${project_root}/project/libs/opencv/modules/videoio/include"
        "${project_root}/project/libs/opencv/modules/highgui/include"
        "${project_root}/project/libs/opencv/modules/objdetect/include"
        "${CMAKE_BINARY_DIR}/third_party/opencv-build"
        PARENT_SCOPE
    )

    if(NOT TARGET Eigen3::Eigen)
        add_library(Eigen3::Eigen INTERFACE IMPORTED GLOBAL)
        set_target_properties(Eigen3::Eigen PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${project_root}/project/libs/eigen"
        )
    endif()

    set(luajit_src_dir "${project_root}/project/libs/luajit/src")
    set(luajit_build_output "${luajit_src_dir}/libluajit.a")
    find_program(MAKE_EXECUTABLE make)
    if(NOT MAKE_EXECUTABLE)
        message(FATAL_ERROR "Could not find 'make' required to build bundled LuaJIT.")
    endif()

    add_custom_command(
        OUTPUT "${luajit_build_output}"
        COMMAND "${CMAKE_COMMAND}" -E env
            "MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}"
            "DEFAULT_CC=clang"
            "HOST_CC=clang"
            "${MAKE_EXECUTABLE}"
        WORKING_DIRECTORY "${luajit_src_dir}"
        COMMENT "Building bundled LuaJIT"
        VERBATIM
    )
    add_custom_target(user_build_luajit DEPENDS "${luajit_build_output}")

    if(NOT TARGET user_luajit)
        add_library(user_luajit STATIC IMPORTED GLOBAL)
        set_target_properties(user_luajit PROPERTIES IMPORTED_LOCATION "${luajit_build_output}")
        add_dependencies(user_luajit user_build_luajit)
    endif()

    # Build OpenCV from bundled source (offline, local submodule/worktree only).
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_LIST "core,imgproc,imgcodecs,videoio,highgui,objdetect" CACHE STRING "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_PERF_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_opencv_apps OFF CACHE BOOL "" FORCE)
    set(BUILD_opencv_world OFF CACHE BOOL "" FORCE)
    set(WITH_FFMPEG OFF CACHE BOOL "" FORCE)
    set(WITH_OPENCL OFF CACHE BOOL "" FORCE)
    set(WITH_IPP OFF CACHE BOOL "" FORCE)
    set(WITH_CUDA OFF CACHE BOOL "" FORCE)
    set(WITH_VTK OFF CACHE BOOL "" FORCE)
    set(WITH_QT OFF CACHE BOOL "" FORCE)
    set(WITH_AVIF OFF CACHE BOOL "" FORCE)
    set(WITH_OPENEXR OFF CACHE BOOL "" FORCE)
    set(OPENCV_IO_FORCE_OPENEXR OFF CACHE BOOL "" FORCE)

    if(NOT TARGET opencv_core)
        add_subdirectory("${project_root}/project/libs/opencv" "${CMAKE_BINARY_DIR}/third_party/opencv-build" EXCLUDE_FROM_ALL)
    endif()

    set(USER_OPENCV_LIBS
        opencv_core
        opencv_imgproc
        opencv_imgcodecs
        opencv_videoio
        opencv_highgui
        opencv_objdetect
        PARENT_SCOPE
    )
endfunction()
