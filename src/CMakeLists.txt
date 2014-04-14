set(TARGET wdfs)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
include_directories(${CMAKE_BINARY_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

find_package(Boost REQUIRED COMPONENTS
    program_options
	serialization
	filesystem
	system
)
include_directories(${Boost_INCLUDE_DIRS})

include_directories(/usr/include/neon)
include_directories(/usr/include/fuse)
include_directories(/usr/include/glib-2.0)
include_directories(/usr/include/glib-2.0/include)
include_directories(/usr/include/glib-2.0/glib)
include_directories(/usr/lib/glib-2.0/include)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wunused")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wunused")

add_definitions(-D_FILE_OFFSET_BITS=64)
add_definitions(-D_GNU_SOURCE -D_REENTRANT)

set(HEADERS
	cache.h
	log.h
	common.h
	config.h
	wdfs-main.h
	wdfs_controller.h
	webdav.h
)

set(SOURCES
	cache.cpp
	log.cpp
	webdav.cpp
	wdfs-main.cpp
	common.cpp
	wdfs_controller.cpp
)

add_executable(${TARGET} ${HEADERS} ${SOURCES})
target_link_libraries(${TARGET} ${Boost_LIBRARIES} pthread neon fuse glib-2.0)

