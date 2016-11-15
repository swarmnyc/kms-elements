#!/bin/bash

BUILD_ROOT=.build

mkdir $BUILD_ROOT
cp -r ../gst-plugins-bad ./$BUILD_ROOT/

cd $BUILD_ROOT
cmake ../.. -DGENERATE_JS_CLIENT_PROJECT=TRUE
make

cd ..
./install_kms_elements.sh

