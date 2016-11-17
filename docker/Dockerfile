# stream oriented kurento
#
# VERSION               4.4.3

FROM      ubuntu:14.04
MAINTAINER Tao Ren <tao@swarmnyc.com>, <tour.ren.gz@gmail.com>

RUN \
    apt-get update && \
    apt-get -y dist-upgrade  && \
    apt-get install -y curl wget

RUN \
    curl -sL https://deb.nodesource.com/setup_4.x | sudo bash - && \
	apt-get update && \
	apt-get install -y nodejs && \
	npm install npm -g && \
	npm install bower -g

RUN \
	apt-get install -y libboost-all-dev libjson-glib-dev bison flex uuid-dev libsoup2.4-dev


RUN	echo "deb http://ubuntu.kurento.org/ trusty kms6" | tee /etc/apt/sources.list.d/kurento.list \
	&& wget -O - http://ubuntu.kurento.org/kurento.gpg.key | apt-key add - \
	&& apt-get update \
	&& apt-get -y install kurento-media-server-6.0 \
	&& apt-get clean \
    && rm -rf /var/lib/apt/lists/*

EXPOSE 8888

COPY ./entrypoint.sh /entrypoint.sh
COPY .build/stylecompositor.tar.gz /stylecompositor.tar.gz
COPY .build/kurento-test-nodejs.tar.gz /kurento-test-nodejs.tar.gz

RUN tar xzvf /stylecompositor.tar.gz && tar xzvf /kurento-test-nodejs.tar.gz


RUN cp /gst-plugins-bad/libgstcompositor.so `dpkg  -L gstreamer1.5-plugins-bad | grep libgstcompositor.so`
RUN cp /src/server/libkmselementsmodule.so `dpkg  -L kms-elements-6.0 | grep libkmselementsmodule.so`
RUN cp /src/server/libkmselementsimpl.so.6 `dpkg  -L kms-elements-6.0 | grep libkmselementsimpl.so.6$`
RUN cp /src/gst-plugins/libkmselementsplugins.so `dpkg  -L kms-elements-6.0 | grep libkmselementsplugins.so`


ENV GST_DEBUG=Kurento*:5,stylecompositemixer:9,compositor:9


ENTRYPOINT ["/entrypoint.sh"]
