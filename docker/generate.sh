#!/bin/bash

BUILD_ROOT=.build

mkdir $BUILD_ROOT
cp -r ../gst-plugins-bad ./$BUILD_ROOT/

cd $BUILD_ROOT
cmake ../.. -DGENERATE_JS_CLIENT_PROJECT=TRUE
make

echo
echo Package all .so files to stylecompositor.tar.gz:
/bin/sleep 3
tar czvf ../stylecompositor.tar.gz src/server/lib* src/gst-plugins/lib* gst-plugins-bad/libgstcompositor.so

