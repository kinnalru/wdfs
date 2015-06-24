set(TARGET qwebdavlib)
project(${TARGET})

cmake_minimum_required(VERSION 2.4)
if(COMMAND cmake_policy)
    cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

if(CMAKE_BUILD_TYPE STREQUAL Debug)
	add_definitions(-DDEBUG)
	add_definitions(-DDEBUG_WEBDAV)
endif()

add_definitions(-DQWEBDAVITEM_EXTENDED_PROPERTIES)
add_definitions(-DQWEBDAV_LIBRARY)

add_definitions("-std=c++0x -std=c++11")

set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt4 REQUIRED QtCore QtNetwork QtXml)
include(${QT_USE_FILE})

set(HEADERS
	qnaturalsort.h
	qwebdavdirparser.h
	qwebdav_global.h
	qwebdav.h
	qwebdavitem.h
)

set(MOC
	qnaturalsort.h
	qwebdavdirparser.h
	qwebdav_global.h
	qwebdav.h
	qwebdavitem.h
)

set(SOURCES 
	qnaturalsort.cpp
	qwebdav.cpp
	qwebdavdirparser.cpp
	qwebdavitem.cpp
)

set(IFACES
)

set(RESOURCES
)

set(DEPS
)

qt4_wrap_cpp(SOURCES ${MOC})
qt4_wrap_ui(SOURCES ${IFACES})
qt4_add_resources(SOURCES ${RESOURCES})

add_library(${TARGET} SHARED ${HEADERS} ${SOURCES})
target_link_libraries(${TARGET} ${QT_LIBRARIES} ${DEPS})



