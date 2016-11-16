#!/bin/bash

BASEDIR=$(dirname "$0")
cd ${BASEDIR}

BUILD_ROOT=.build

mkdir ${BUILD_ROOT} 2>> /dev/null

cd ${BUILD_ROOT}
cmake ../.. -DGENERATE_JS_CLIENT_PROJECT=TRUE
make

cd ${BASEDIR}
./install_kms_elements.sh

