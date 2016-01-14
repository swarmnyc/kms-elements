sudo service  kurento-media-server-6.0  stop
sudo rm /var/log/kurento-media-server/media-server_201*
make
sudo service  kurento-media-server-6.0  start

touch ../kurento-test-nodejs/server.js 

