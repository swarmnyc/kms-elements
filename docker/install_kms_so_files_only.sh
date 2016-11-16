#!/bin/bash

sudo cp -n `dpkg  -L gstreamer1.5-plugins-bad | grep libgstcompositor.so` `dpkg  -L gstreamer1.5-plugins-bad | grep libgstcompositor.so`.bak
sudo cp -n `dpkg  -L kms-elements-6.0 | grep libkmselementsmodule.so` `dpkg  -L kms-elements-6.0 | grep libkmselementsmodule.so`.bak
sudo cp -n `dpkg  -L kms-elements-6.0 | grep libkmselementsimpl.so.6$` `dpkg  -L kms-elements-6.0 | grep libkmselementsimpl.so.6$`.bak
sudo cp -n `dpkg  -L kms-elements-6.0 | grep libkmselementsplugins.so` `dpkg  -L kms-elements-6.0 | grep libkmselementsplugins.so`.bak

[ -e .build/gst-plugins-bad/libgstcompositor.so ] && cd .build

sudo cp gst-plugins-bad/libgstcompositor.so `dpkg  -L gstreamer1.5-plugins-bad | grep libgstcompositor.so`
sudo cp src/server/libkmselementsmodule.so `dpkg  -L kms-elements-6.0 | grep libkmselementsmodule.so`
sudo cp src/server/libkmselementsimpl.so.6 `dpkg  -L kms-elements-6.0 | grep libkmselementsimpl.so.6$`
sudo cp src/gst-plugins/libkmselementsplugins.so `dpkg  -L kms-elements-6.0 | grep libkmselementsplugins.so`

