#!/bin/bash
git submodule init
git submodule update
cp tools/CMakeLists.txt qwebdavlib/qwebdavlib
mkdir -p build && cd build && cmake .. && make
