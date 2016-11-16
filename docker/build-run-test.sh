#!/bin/bash

# check docker-engine installed.
docker_installed=`dpkg -s docker-engine 2>> /dev/null | grep install`
if [ "" == "$docker_installed" ]; then
    echo docker-engine not installed.
    echo run ./install_docker.sh to install docker first.
    exit
fi

image_tag=apptalks/kurento:6.6.1
container_name=kurento
sudo docker build -t $image_tag .

/bin/sleep 2
echo
echo run the docker image: $image_tag
echo
/bin/sleep 2 
sudo docker stop $container_name
sudo docker rm $container_name
sudo docker run -d --name $container_name -p 8888:8888 $image_tag

/bin/sleep 2
echo
echo test the kurento server.
echo
/bin/sleep 2 
curl -i -N -H "Connection: Upgrade" -H "Upgrade: websocket" -H "Host: 127.0.0.1:8888" -H "Origin: 127.0.0.1" http://127.0.0.1:8888 
sudo docker logs $container_name

