# stream oriented kurento
#
# VERSION               4.4.3

FROM      ubuntu:14.04
MAINTAINER Patxi Gortázar <patxi.gortazar@gmail.com>

RUN apt-get update \
  && apt-get -y dist-upgrade \
	&& apt-get install -y wget \
	&& apt-get install -y libboost-all-dev libjson-glib-dev bison flex uuid-dev libsoup2.4-dev 

RUN	echo "deb http://ubuntu.kurento.org/ trusty kms6" | tee /etc/apt/sources.list.d/kurento.list \
	&& wget -O - http://ubuntu.kurento.org/kurento.gpg.key | apt-key add - \
	&& apt-get update \
	&& apt-get -y install kurento-media-server-6.0 \
	&& apt-get clean \
  && rm -rf /var/lib/apt/lists/*

EXPOSE 8888

COPY ./entrypoint.sh /entrypoint.sh

RUN mkdir /app && mkdir /app/server && mkdir /app/gst-plugins && mkdir /app/gst-plugins-bad
COPY .build/src/server/lib* /app/server/
COPY .build/src/gst-plugins/lib* /app/gst-plugins/
COPY .build/gst-plugins-bad/libgstcompositor.so /app/gst-plugins-bad/
RUN cp /app/gst-plugins-bad/libgstcompositor.so `dpkg  -L gstreamer1.5-plugins-bad | grep libgstcompositor.so`

ENV GST_DEBUG=Kurento*:5
ENV KURENTO_MODULES_PATH=/app/server
ENV GST_PLUGIN_PATH=/app/gst-plugins


ENTRYPOINT ["/entrypoint.sh"]
