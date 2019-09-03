project(ui)

file(GLOB UI_HEADERS
    "*.h"
    "kernel/*.h"
    "as/*.h"
    "datasources/*.h"
    "widgets/*.h"
    "decorators/*.h"
    "parsers/*.h"
	"formatters/*.h"
	"../gameshared/q_*.h"
	"../qalgo/*.h"
)

file(GLOB UI_SOURCES
    "*.cpp"
    "kernel/*.cpp"
    "as/*.cpp"
    "datasources/*.cpp"
    "widgets/*.cpp"
    "decorators/*.cpp"
    "parsers/*.cpp"
    "../gameshared/q_*.c"
	"../qalgo/*.c"
)


file(GLOB NANOSVG_HEADERS
	"${NANOSVG_INCLUDE_DIR}/*.h"
)

source_group("nanosvg\\Header Files" FILES ${NANOSVG_HEADERS})

include_directories(${ANGELSCRIPT_INCLUDE_DIR} ${LIBROCKET_INCLUDE_DIR} ${NANOSVG_INCLUDE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}) # for ui_precompiled.h

add_library(ui SHARED ${NANOSVG_HEADERS} ${UI_SOURCES} ${UI_HEADERS})
target_link_libraries(ui PRIVATE)
add_dependencies(ui angelwrap)

option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(NO_FONT_INTERFACE_DEFAULT "Do not build the font providers that come with the library" ON)
add_subdirectory(${LIBROCKET_DIR} ${CMAKE_HOME_DIRECTORY}/build/ui/rmlui/)

qf_set_output_dir(ui libs)
