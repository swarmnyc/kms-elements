#!/bin/bash

BASEDIR=$(dirname "$0")
cd ${BASEDIR}

BUILD_ROOT=.build

mkdir ${BUILD_ROOT} 2>> /dev/null
cp -r ../gst-plugins-bad ./${BUILD_ROOT}/

# go inside .build/
cd ${BUILD_ROOT}
../install_kms_so_files_only.sh ${BUILD_ROOT}

echo
echo Package all .so files to stylecompositor.tar.gz:
/bin/sleep 2
rm ../stylecompositor.tar.gz
tar czvf ../stylecompositor.tar.gz src/server/lib* src/gst-plugins/lib* gst-plugins-bad/libgstcompositor.so ../install_kms_so_files_only.sh

echo
echo copy nodejs module to kurento-test-nodejs...
/bin/sleep 2
rm ../stylecompositor_js.tar.gz
tar czvf ../stylecompositor_js.tar.gz ./js/*
cp -r ./js/* ../../kurento-test-nodejs/node_modules/kurento-client/node_modules/kurento-client-elements/

