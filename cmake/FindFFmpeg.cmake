# FindFFmpeg.cmake - Find FFmpeg libraries using pkg-config
include(FindPackageHandleStandardArgs)

set(FFMPEG_COMPONENTS avcodec avformat avutil swscale swresample)

set(FFMPEG_INCLUDE_DIRS "")
set(FFMPEG_LIBRARIES "")

foreach(comp ${FFMPEG_COMPONENTS})
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FF_${comp} QUIET lib${comp})
    endif()

    find_path(${comp}_INCLUDE_DIR
        NAMES lib${comp}/${comp}.h
        HINTS ${FF_${comp}_INCLUDE_DIRS}
        PATH_SUFFIXES ffmpeg
    )

    # libavcodec header is avcodec.h, libavformat is avformat.h, etc.
    find_library(${comp}_LIBRARY
        NAMES ${comp}
        HINTS ${FF_${comp}_LIBRARY_DIRS}
    )

    if(${comp}_INCLUDE_DIR AND ${comp}_LIBRARY)
        set(FFmpeg_${comp}_FOUND TRUE)
        list(APPEND FFMPEG_INCLUDE_DIRS ${${comp}_INCLUDE_DIR})
        list(APPEND FFMPEG_LIBRARIES ${${comp}_LIBRARY})

        if(NOT TARGET FFmpeg::${comp})
            add_library(FFmpeg::${comp} IMPORTED INTERFACE)
            set_target_properties(FFmpeg::${comp} PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${${comp}_INCLUDE_DIR}"
                INTERFACE_LINK_LIBRARIES "${${comp}_LIBRARY}"
            )
        endif()
    endif()
endforeach()

list(REMOVE_DUPLICATES FFMPEG_INCLUDE_DIRS)

find_package_handle_standard_args(FFmpeg
    REQUIRED_VARS FFMPEG_LIBRARIES FFMPEG_INCLUDE_DIRS
    HANDLE_COMPONENTS
)

if(FFmpeg_FOUND AND NOT TARGET FFmpeg::FFmpeg)
    add_library(FFmpeg::FFmpeg IMPORTED INTERFACE)
    set_target_properties(FFmpeg::FFmpeg PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES "${FFMPEG_LIBRARIES}"
    )
endif()
