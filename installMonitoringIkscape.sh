#!/bin/bash

mkdir groupSixInkscape
cd groupSixInkscape
git clone https://github.com/batincanbalkan/betterInkscape.git

cd master
mkdir build
cd build
cmake ..
make
make install


