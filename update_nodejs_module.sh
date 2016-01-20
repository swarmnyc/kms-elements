cd src/
cmake .. -DGENERATE_JS_CLIENT_PROJECT=TRUE
make
cd ../kurento-test-nodejs
sudo rm -rf ./node_modules/kurento-client/node_modules/kurento-client-elements_origin/
sudo mv ./node_modules/kurento-client/node_modules/kurento-client-elements/ ./node_modules/kurento-client/node_modules/kurento-client-elements_origin/
sudo cp -r ../src/js/ ./node_modules/kurento-client/node_modules/kurento-client-elements/


