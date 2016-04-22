sudo rm /var/log/kurento-media-server/media-server_201*
cd ./src
sudo service kurento-media-server-6.0 stop
make
sudo service  kurento-media-server-6.0  start

cd ..
touch ./kurento-test-nodejs/server.js 

sudo service  kurento-media-server-6.0  status
ps -A | grep kurento

