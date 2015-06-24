set(TARGET wdfs)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
include_directories(${CMAKE_BINARY_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

#find_package(Boost REQUIRED COMPONENTS
#    program_options
#	serialization
#	filesystem
#	system
#)
#include_directories(${Boost_INCLUDE_DIRS})

include_directories(/usr/include/fuse)

find_package(Qt4 REQUIRED QtCore QtNetwork QtXml)
include(${QT_USE_FILE})

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wunused")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wunused")

add_definitions(-D_FILE_OFFSET_BITS=64)
add_definitions(-D_GNU_SOURCE -D_REENTRANT)

set(HEADERS
	common.h
	wdfs-main.h
)

set(SOURCES
	wdfs-main.cpp
	common.cpp
)

add_executable(${TARGET} ${HEADERS} ${SOURCES})
target_link_libraries(${TARGET} ${Boost_LIBRARIES} ${QT_LIBRARIES} pthread fuse)

