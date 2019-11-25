#!/bin/bash
mkdir originalInkscape
cd originalInkscape
git clone --recurse-submodules https://gitlab.com/inkscape/inkscape.git master

#for macOS X
# you may encounter issues such as adding new dependencies
cd master
mkdir build
cd build
cmake ..
make
make install

inkscape