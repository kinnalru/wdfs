project(wdfs_top)
cmake_minimum_required(VERSION 2.4)

if(COMMAND cmake_policy)
    cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

add_definitions("-std=c++0x")

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(qwebdavlib/qwebdavlib)
add_subdirectory(src)

