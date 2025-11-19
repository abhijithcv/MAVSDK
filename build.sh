#!/bin/bash
cmake -DCMAKE_BUILD_TYPE=Debug -Bbuild -S.
cmake --build build -j8
sudo cmake --build build --target install
sudo ldconfig
