How to merge to original repository supported by kurento team.
-------
```
# Clone swarm kms repository
git clone https://github.com/swarmnyc/kms-elements.git -b swarm_composite_6.2
# Add another remote repository supported by kurento team.
git remote add kms-origin https://github.com/Kurento/kms-elements.git
git remote update
# create a new branch to do the merge job.
git checkout -b swarm_kms_6.4
# merge to tag:6.4.0
git merge 6.4.0
# fix some conflicts and then you can rebuild the project(see below)
# ...
# commit the merge if everything is cool.
git commit -am "Merge to 6.4.0"
```

2nd way to merge to original repository supported by kurento team.
-------
```
# Clone swarm kms repository
git clone https://github.com/swarmnyc/kms-elements.git -b swarm_kms_6.4
# Add another remote repository supported by kurento team.
git remote add kms-origin https://github.com/Kurento/kms-elements.git
git remote update
# compare HEAD([branch]swarm_kms_6.4) on [tag]6.4.0, and zip all different files.
tar -czvf ../kms-elements-HEAD_on_6.4.0.tar.gz `git diff --name-only 6.4.0`
# checkout most update [tag]6.6.1
git checkout  6.6.1
# unzip all different(between HEAD and 6.4.0) files to it.
tar -xzvf kms-elements-HEAD_on_6.4.0.tar.gz kms-elements/
# fix some conflicts and then you can rebuild the project(see below)
...
# add missing files
cd kms-elements/
git add `tar tf ../kms-elements-HEAD_on_6.4.0.tar.gz`
# commit the merge if everything is cool.
git commit -m "Rebase swarm_kms_6.4 from [tag]6.4.0 to [tag]6.6.1"
```

3rd way to merge to original repository supported by kurento team.
-------
```
# Clone swarm kms repository
git clone https://github.com/swarmnyc/kms-elements.git -b swarm_kms_6.4
# Add another remote repository supported by kurento team.
git remote add kms-origin https://github.com/Kurento/kms-elements.git
git remote update
git rebase 6.6.1
# fix some conflicts and then you can rebuild the project(see below)

```

Build kms-elements
-------
#### install dependent packages.
```
# install kurento-media-server-dev
echo "deb http://ubuntu.kurento.org trusty-dev kms6" | sudo tee /etc/apt/sources.list.d/kurento-dev.list
wget -O - http://ubuntu.kurento.org/kurento.gpg.key | sudo apt-key add -
sudo apt-get update
sudo apt-get install kurento-media-server-6.0-dev
sudo apt-get install libboost-all-dev libjson-glib-dev bison flex uuid-dev libsoup2.4-dev build-essential libtool autotools-dev  automake indent astyle npm nodejs-legacy git
# build tools
#   build-essential libtool autotools-dev automake
# for kurento-element to commit to git.
#   indent astyle
# for node server.
#   npm nodejs-legacy
```

#### build gstreamer-sctp-1.5
```
git clone https://github.com/Kurento/usrsctp.git
cd usrsctp/
./bootstrap
./configure --prefix=/usr
make
sudo make install
cd ..
git clone https://github.com/Kurento/openwebrtc-gst-plugins.git
cd openwebrtc-gst-plugins/
./autogen.sh
./configure --prefix=/usr
make
sudo make install
```

### start to build kms-elements
```
git clone https://github.com/swarmnyc/kms-elements.git -b swarm_composite_6.2
cd kms-elements/src
# checkout by the version tag which specified by kurento-media-server.(check the log for the version)
cmake ..
make

# generate the js code used by npm[node.js]
cmake .. -DGENERATE_JS_CLIENT_PROJECT=TRUE
cp -r ./js/ <where_your_application_server>/node_modules/kurento-client/node_modules/kurento-client-elements/
```

### config kurento server to use the new build kms-elements plugins.
```
vi /etc/default/kurento-media-server-6.0
  (Add below 2 lines)
  export KURENTO_MODULES_PATH=/home/vagrant/kms-elements/src/src/server
  export GST_PLUGIN_PATH=/home/vagrant/kms-elements/src/src/gst-plugins
```

### For deployment in app talks
replace shared_node_modules/swarm_composite/kurento-client-elements with the js folder generated


### test using node.js server
```
sudo service kurento-media-server-6.0 restart

cd ../kurento-test-nodejs/
npm install
# copy new version js file to current appplication server's node_modules.
mv ./node_modules/kurento-client/node_modules/kurento-client-elements/ ./node_modules/kurento-client/node_modules/kurento-client-elements_origin/
cp -r ../src/js/ ./node_modules/kurento-client/node_modules/kurento-client-elements/
# build a local background image.
curl http://placeimg.com/800/600/any.jpg > bg.jpg
sudo mv bg.jpg /etc/kurento/
node server.js 
```
### install the updated compositor plugin, built from [Swarm gst-plugins-bad]
```
# replace the original compositor plugin, which will expand the output video's resolution according to the content from hubport.
sudo cp  ./gst-plugins-bad/libgstcompositor.so /usr/lib/x86_64-linux-gnu/gstreamer-1.5/
```

[Swarm gst-plugins-bad]: https://github.com/swarmnyc/gst-plugins-bad
