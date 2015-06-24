#!/bin/bash
git submodule init
git submodule update
ln -s  `pwd`/tools/CMakeLists.txt qwebdavlib/qwebdavlib
mkdir -p build && cd build && cmake .. && make
