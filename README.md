[![][KurentoImage]][Kurento]

Copyright © 2013-2016 [Kurento]. Licensed under [LGPL v2.1 License].

kms-elements
============

Elements for Kurento Media Server.

The kms-elements project contains **elements** needed for the Kurento Media
Server.


Build kms-elements
-------
#### install dependent packages.
```
# install kurento-media-server-dev
echo "deb http://ubuntu.kurento.org trusty-dev kms6" | sudo tee /etc/apt/sources.list.d/kurento-dev.list
wget -O - http://ubuntu.kurento.org/kurento.gpg.key | sudo apt-key add -
sudo apt-get update
sudo apt-get install kurento-media-server-6.0-dev

sudo apt-get install libboost-all-dev bison flex uuid-dev libsoup2.4-dev build-essential libtool autotools-dev  automake indent astyle npm nodejs-legacy git
sudo apt-get install libjson-glib-dev

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



What is Kurento
---------------

Kurento is an open source software project providing a platform suitable 
for creating modular applications with advanced real-time communication
capabilities. For knowing more about Kurento, please visit the Kurento
project website: http://www.kurento.org.

Kurento is part of [FIWARE]. For further information on the relationship of 
FIWARE and Kurento check the [Kurento FIWARE Catalog Entry]

Kurento is part of the [NUBOMEDIA] research initiative.

Documentation
-------------

The Kurento project provides detailed [documentation] including tutorials,
installation and development guides. A simplified version of the documentation
can be found on [readthedocs.org]. The [Open API specification] a.k.a. Kurento
Protocol is also available on [apiary.io].

Source
------

Code for other Kurento projects can be found in the [GitHub Kurento Group].

News and Website
----------------

Check the [Kurento blog]
Follow us on Twitter @[kurentoms].

Issue tracker
-------------

Issues and bug reports should be posted to the [GitHub Kurento bugtracker]

Licensing and distribution
--------------------------

Software associated to Kurento is provided as open source under GNU Library or
"Lesser" General Public License, version 2.1 (LGPL-2.1). Please check the
specific terms and conditions linked to this open source license at
http://opensource.org/licenses/LGPL-2.1. Please note that software derived as a
result of modifying the source code of Kurento software in order to fix a bug
or incorporate enhancements is considered a derivative work of the product.
Software that merely uses or aggregates (i.e. links to) an otherwise unmodified
version of existing software is not considered a derivative work.

Contribution policy
-------------------

You can contribute to the Kurento community through bug-reports, bug-fixes, new
code or new documentation. For contributing to the Kurento community, drop a
post to the [Kurento Public Mailing List] providing full information about your
contribution and its value. In your contributions, you must comply with the
following guidelines

* You must specify the specific contents of your contribution either through a
  detailed bug description, through a pull-request or through a patch.
* You must specify the licensing restrictions of the code you contribute.
* For newly created code to be incorporated in the Kurento code-base, you must
  accept Kurento to own the code copyright, so that its open source nature is
  guaranteed.
* You must justify appropriately the need and value of your contribution. The
  Kurento project has no obligations in relation to accepting contributions
  from third parties.
* The Kurento project leaders have the right of asking for further
  explanations, tests or validations of any code contributed to the community
  before it being incorporated into the Kurento code-base. You must be ready to
  addressing all these kind of concerns before having your code approved.

Support
-------

The Kurento project provides community support through the  [Kurento Public
Mailing List] and through [StackOverflow] using the tags *kurento* and
*fiware-kurento*.

Before asking for support, please read first the [Kurento Netiquette Guidelines]

[documentation]: http://www.kurento.org/documentation
[FIWARE]: http://www.fiware.org
[GitHub Kurento bugtracker]: https://github.com/Kurento/bugtracker/issues
[GitHub Kurento Group]: https://github.com/kurento
[kurentoms]: http://twitter.com/kurentoms
[Kurento]: http://kurento.org
[Kurento Blog]: http://www.kurento.org/blog
[Kurento FIWARE Catalog Entry]: http://catalogue.fiware.org/enablers/stream-oriented-kurento
[Kurento Netiquette Guidelines]: http://www.kurento.org/blog/kurento-netiquette-guidelines
[Kurento Public Mailing list]: https://groups.google.com/forum/#!forum/kurento
[KurentoImage]: https://secure.gravatar.com/avatar/21a2a12c56b2a91c8918d5779f1778bf?s=120
[LGPL v2.1 License]: http://www.gnu.org/licenses/lgpl-2.1.html
[NUBOMEDIA]: http://www.nubomedia.eu
[StackOverflow]: http://stackoverflow.com/search?q=kurento
[Read-the-docs]: http://read-the-docs.readthedocs.org/
[readthedocs.org]: http://kurento.readthedocs.org/
[Open API specification]: http://kurento.github.io/doc-kurento/
[apiary.io]: http://docs.streamoriented.apiary.io/
