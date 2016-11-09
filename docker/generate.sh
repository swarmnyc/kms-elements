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
rm ../stylecompositor.tar.gz
tar czvf ../stylecompositor.tar.gz src/server/lib* src/gst-plugins/lib* gst-plugins-bad/libgstcompositor.so

echo
echo copy nodejs module to kurento-test-nodejs...
/bin/sleep 2
rm ../stylecompositor_js.tar.gz
tar czvf ../stylecompositor_js.tar.gz ./js/*
cp -r ./js/* ../../kurento-test-nodejs/node_modules/kurento-client/node_modules/kurento-client-elements/
