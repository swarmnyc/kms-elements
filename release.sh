js_target=kurento-client-elements.tar.gz
cd src/
cmake .. -DGENERATE_JS_CLIENT_PROJECT=TRUE
cd js/
tar -czvf $js_target *
mv $js_target ../../

target=kms-elements-release.tar.gz
cd ../
make
cd src/
tar -czvf $target `find . | grep "\.so"$`
mv $target ../../


echo
echo release to azure cloud server...

cd ../../
scp $target swarmnyc@kurentotest.cloudapp.net:~/

