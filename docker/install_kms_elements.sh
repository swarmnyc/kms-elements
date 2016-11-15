sudo cp -n `dpkg  -L gstreamer1.5-plugins-bad | grep libgstcompositor.so` `dpkg  -L gstreamer1.5-plugins-bad | grep libgstcompositor.so`.bak
sudo cp -n `dpkg  -L kms-elements-6.0 | grep libkmselementsmodule.so` `dpkg  -L kms-elements-6.0 | grep libkmselementsmodule.so`.bak
sudo cp -n `dpkg  -L kms-elements-6.0 | grep libkmselementsimpl.so.6$` `dpkg  -L kms-elements-6.0 | grep libkmselementsimpl.so.6$`.bak
sudo cp -n `dpkg  -L kms-elements-6.0 | grep libkmselementsplugins.so` `dpkg  -L kms-elements-6.0 | grep libkmselementsplugins.so`.bak

BUILD_ROOT=.build


cp -r ../gst-plugins-bad ./$BUILD_ROOT/
cd $BUILD_ROOT/
sudo cp gst-plugins-bad/libgstcompositor.so `dpkg  -L gstreamer1.5-plugins-bad | grep libgstcompositor.so`
sudo cp src/server/libkmselementsmodule.so `dpkg  -L kms-elements-6.0 | grep libkmselementsmodule.so`
sudo cp src/server/libkmselementsimpl.so.6 `dpkg  -L kms-elements-6.0 | grep libkmselementsimpl.so.6$`
sudo cp src/gst-plugins/libkmselementsplugins.so `dpkg  -L kms-elements-6.0 | grep libkmselementsplugins.so`

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

