/*
 * (C) Copyright 2014-2015 Kurento (http://kurento.org/)
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 */

var path = require('path');
var url = require('url');
var express = require('express');
var minimist = require('minimist');
var ws = require('ws');
var kurento = require('kurento-client');
var fs    = require('fs');
var https = require('https');

var argv = minimist(process.argv.slice(2), {
	default: {
		as_uri: 'https://localhost:8443/',
		ws_uri: 'ws://localhost:8888/kurento'
	}
});

var options =
{
	key:  fs.readFileSync('keys/server.key'),
	cert: fs.readFileSync('keys/server.crt')
};

var style = {width:1280, height:720, 'frame-rate':12, 'pad-y':160, background:"http://placeimg.com/800/600/any.jpg", views:[]};
//var style = {width:1280, height:720, 'pad-y':160, background:"https://s3.amazonaws.com/apptalksqa/defaultBack.jpg", views:[]};
var views_style = {views:[{width:640, height:480, text:"123"},{width:320, height:480, text:"1234"},{id:3},{text:"abc"}]};
var app = express();

/*
 * Definition of global variables.
 */
var idCounter = 1234;
var candidatesQueue = {};
var kurentoClient = null;
var presenter = null;
var viewers = [];
var noPresenterMessage = 'No active presenter. Try again later...';
var presenter = {
	pipeline : null,
	composite: null,
	webRtcEndpoint : {},
	hubPorts : {}
}

/*
 * Server startup
 */
var asUrl = url.parse(argv.as_uri);
var port = asUrl.port;
var server = https.createServer(options, app).listen(port, function() {
	console.log('Kurento Tutorial started');
	console.log('Open ' + url.format(asUrl) + ' with a WebRTC capable browser');
});

var wss = new ws.Server({
	server : server,
	path : '/one2many'
});

function nextUniqueId() {
	idCounter++;
	return idCounter.toString();
}

/*
 * Management of WebSocket messages
 */
wss.on('connection', function(ws) {

	var sessionId = nextUniqueId();
	//console.log('Connection received with sessionId ' + sessionId);

	ws.on('error', function(error) {
		console.log('Connection ' + sessionId + ' error');
		stop(sessionId);
	});

	ws.on('close', function() {
		console.log('Connection ' + sessionId + ' closed');
		stop(sessionId);
	});

	ws.on('message', function(_message) {
		var message = JSON.parse(_message);
//		console.log('Connection ' + sessionId + ' received message ', message);

		switch (message.id) {
			case 'presenter':
				startPresenter(sessionId, ws, message.sdpOffer, function(error, sdpAnswer) {
					if (error) {
						return ws.send(JSON.stringify({
							id : 'presenterResponse',
							response : 'rejected',
							message : error
						}));
					}
					ws.send(JSON.stringify({
						id : 'presenterResponse',
						response : 'accepted',
						sdpAnswer : sdpAnswer
					}));
				});
				break;

			case 'viewer':
				startViewer(sessionId, ws, message.sdpOffer, function(error, sdpAnswer) {
					if (error) {
						return ws.send(JSON.stringify({
							id : 'viewerResponse',
							response : 'rejected',
							message : error
						}));
					}

					ws.send(JSON.stringify({
						id : 'viewerResponse',
						response : 'accepted',
						sdpAnswer : sdpAnswer
					}));
				});
				break;

			case 'stop':
				stop(sessionId);
				break;

			case 'onIceCandidate':
				onIceCandidate(sessionId, message.candidate);
				break;

			case 'action1':
				presenter.composite.showView(parseInt(sessionId));
				if (presenter.composite != null) {
					console.log("getGStreamerDot");
					presenter.pipeline.getGstreamerDot(function(err, ret){
						if(err) {
							return console.log(err);
						}
						var fs = require('fs');
						fs.writeFile("pipeline.dot", ret, function(err) {
							if(err) {
								return console.log(err);
							}
							console.log("The file pipeline.dot was saved!");
						});
					});
					presenter.composite.getGstreamerDot(function(err, ret){
						if(err) {
							return console.log(err);
						}
						var fs = require('fs');
						fs.writeFile("composite.dot", ret, function(err) {
							if(err) {
								return console.log(err);
							}
							console.log("The file composite.dot was saved!");
						});
					});
					if (presenter.webRtcEndpoint[sessionId]) {
						presenter.webRtcEndpoint[sessionId].getGstreamerDot(function (err, ret) {
							if (err) {
								return console.log(err);
							}
							var fs = require('fs');
							fs.writeFile("webrtcEndpointPresenter.dot", ret, function (err) {
								if (err) {
									return console.log(err);
								}
								console.log("The file webrtcEndpointPresenter.dot was saved!");
							});
						});
					}
					if (viewers[sessionId] && viewers[sessionId].webRtcEndpoint) {
						viewers[sessionId].webRtcEndpoint.getGstreamerDot(function (err, ret) {
							if (err) {
								return console.log(err);
							}
							var fs = require('fs');
							fs.writeFile("webrtcEndpointViewer.dot", ret, function (err) {
								if (err) {
									return console.log(err);
								}
								console.log("The file webrtcEndpointViewer.dot was saved!");
							});
						});
					}
					//presenter.composite.setStyle("{'background':'http://placeimg.com/640/480/any.jpg'}");
					//views_style.views[0].width += 64;
					//views_style.views[0].height += 48;
					//presenter.composite.setStyle(JSON.stringify(views_style));
				}
				ws.send(JSON.stringify({
					id : 'action1',
					message : 'accepted'
				}));
				break;

			case 'action2':
				presenter.composite.hideView(parseInt(sessionId));
				//if (presenter.composite != null) {
				//	views_style.views[0].width -= 64;
				//	views_style.views[0].height -= 48;
				//	presenter.composite.setStyle(JSON.stringify(views_style));
                //
				//	//var style = {background:"http://placeimg.com/800/600/any.jpg"};
				//	//presenter.composite.setStyle(JSON.stringify(style));
				//}
				ws.send(JSON.stringify({
					id : 'action2',
					message : 'accepted'
				}));
				break;

			default:
				ws.send(JSON.stringify({
					id : 'error',
					message : 'Invalid message ' + message
				}));
				break;
		}
	});
});

/*
 * Definition of functions
 */

// Recover kurentoClient for the first time.
function getKurentoClient(callback) {
	if (kurentoClient !== null) {
		return callback(null, kurentoClient);
	}

	kurento(argv.ws_uri, function(error, _kurentoClient) {
		if (error) {
			console.log("Could not find media server at address " + argv.ws_uri);
			return callback("Could not find media server at address" + argv.ws_uri
				+ ". Exiting with error " + error);
		}

		kurentoClient = _kurentoClient;
		callback(null, kurentoClient);
	});
}

function startPresenter(sessionId, ws, sdpOffer, callback) {
	clearCandidatesQueue(sessionId);

	getKurentoClient(function(error, kurentoClient) {
		if (presenter.pipeline === null) {
			kurentoClient.create('MediaPipeline', function(error, _pipeline) {
				if (error) {
					return callback(error);
				}
				presenter.pipeline = _pipeline;

				presenter.pipeline.create('StyleComposite', function(error, _composite) {
					if (error) {
						return callback(error);
					}
					//var style = {width:800, height:600, 'pad-y':160, background:"http://placeimg.com/800/600/any.jpg", views]};
					_composite.setStyle(JSON.stringify(style));
					_composite.getStyle(function(err, ret) {
						console.log( "getStyle return:" + ret );
					});
					//_composite.setBackgroundImage("/etc/kurento/bg.jpg");//("http://placeimg.com/800/600/any.jpg");
					presenter.composite = _composite;
					//addPlayer(sessionId, ws, sdpOffer, callback);
					addPresenter(sessionId, ws, sdpOffer, callback);
				});
			});
		} else {
			addPresenter(sessionId, ws, sdpOffer, callback);
		}
	});
}

/*
 function startPresenter(sessionId, ws, sdpOffer, callback) {
 clearCandidatesQueue(sessionId);

 getKurentoClient(function(error, kurentoClient) {
 if (error) {
 stop(sessionId);
 return callback(error);
 }

 if (presenter === null) {
 stop(sessionId);
 return callback(noPresenterMessage);
 }

 kurentoClient.create('MediaPipeline', function(error, pipeline) {
 if (error) {
 stop(sessionId);
 return callback(error);
 }

 if (presenter === null) {
 stop(sessionId);
 return callback(noPresenterMessage);
 }

 presenter.pipeline = pipeline;
 presenter.pipeline.create('Composite', function(error, _composite) {
 if (error) {
 return callback(error);
 }
 presenter.composite = _composite;
 addPresenter(sessionId, ws, sdpOffer, callback);
 });

 });
 });
 }
 */
//var playerURI="http://www.youtubeinmp4.com/redirect.php?video=x_cmZyYHMTk&r=D396bQeGXWJJXzm6y0qWbDPg8A0mWpViMlVWDWE76T4%3D";
var playerURI="https://192.168.16.112:8443/sample.mp4";
function addPlayer(sessionId, ws, sdpOffer, callback) {
    // Create player
    console.log('Creating PlayerEndpoint');
    presenter.pipeline.create('PlayerEndpoint', {uri: playerURI}, function(error, playerEndpoint) {
        if (error) return wsError(ws, "ERROR 3: " + error);

        playerEndpoint.on('EndOfStream', function() {
            console.log('END OF STREAM');
            pipeline.release();
        });

        console.log('Now Playing: ' + playerURI);
        playerEndpoint.play(function(error) {
            if (error) return wsError(ws, "ERROR 4: " + error);

            presenter.composite.createHubPort(function (error, _hubPort) {
                    if (error) {
                            console.log("error creating hubport for participant");
                            return calback(error);
                    }
                    presenter.hubPorts[sessionId] = _hubPort;
                    style.views.push({id:parseInt(sessionId), width:800, height:600, text:"ID:"+sessionId});
                    presenter.composite.setStyle(JSON.stringify(style));

                    _hubPort.setMaxOutputBitrate(parseInt(sessionId), function(err, obj){
                            _hubPort.getMaxOutputBitrate(function(err, obj){
                                    console.log("hubPort.getMaxOuputBitrate(session id) = " + obj);
                            });

                    });
                    playerEndpoint.connect(_hubPort, function() {
        				console.log("PlayerEndpoint connected to hubport.");
		        	});
		    });
        });

	});

}

function addPresenter(sessionId, ws, sdpOffer, callback) {
	console.warn("startPresenter sid=" + sessionId);
	presenter.pipeline.create('WebRtcEndpoint', function(error, webRtcEndpoint) {
		if (error) {
			stop(sessionId);
			return callback(error);
		}

		console.log("addPresenter " + sessionId + " : " + webRtcEndpoint);
		presenter.webRtcEndpoint[sessionId] = webRtcEndpoint;
		presenter.composite.createHubPort(function (error, _hubPort) {
			if (error) {
				console.log("error creating hubport for participant");
				return calback(error);
			}
			presenter.hubPorts[sessionId] = _hubPort;
			//_hubPort.getName(function(err, obj){
			//	console.log("hubPort.getId");
			//	console.log(err);
			//	console.log(obj);
			//});
			//_hubPort.getGstreamerDot(function(err, obj){
			//	console.log("hubPort.getGstremerDot");
			//	console.log(err);
			//	console.log(obj);
			//});
			if (sessionId <= 1236) style.views.push({id:parseInt(sessionId), width:128, height:72, text:"ID:"+sessionId});
			else style.views.push({id:parseInt(sessionId), width:800, height:600, text:"ID:"+sessionId});
			presenter.composite.setStyle(JSON.stringify(style));
			
			_hubPort.setMaxOuputBitrate(parseInt(sessionId), function(err, obj){
				_hubPort.getMaxOuputBitrate(function(err, obj){
					console.log("hubPort.getMaxOuputBitrate(session id) = " + obj);
				});

			});
			webRtcEndpoint.connect(_hubPort);
//			presenter.composite.setStyle(JSON.stringify({text:'.                    BG                           .'}));
//
//            presenter.pipeline.create('TextOverlay', function(error, _textoverlay) {
//                if (error) {
//                    return callback(error);
//                }
//				var style = {text: 'I am '+sessionId, 'font-desc': 'sans bold 24', deltay:30};
////				_textoverlay.setStyle(JSON.stringify(style));
//                console.log("TextOverlay:" + JSON.stringify(_textoverlay));
//                //webRtcEndpoint.connect(_textoverlay);
//                //_textoverlay.connect(_hubPort);
//                webRtcEndpoint.connect(_hubPort);
//            });
		});

		if (candidatesQueue[sessionId]) {
			while(candidatesQueue[sessionId].length) {
				var candidate = candidatesQueue[sessionId].shift();
				webRtcEndpoint.addIceCandidate(candidate);
			}
		}

		webRtcEndpoint.on('OnIceCandidate', function(event) {
			var candidate = kurento.register.complexTypes.IceCandidate(event.candidate);
			ws.send(JSON.stringify({
				id : 'iceCandidate',
				candidate : candidate
			}));
		});

		webRtcEndpoint.processOffer(sdpOffer, function(error, sdpAnswer) {
			if (error) {
				stop(sessionId);
				return callback(error);
			}

			if (presenter === null) {
				stop(sessionId);
				return callback(noPresenterMessage);
			}

			callback(null, sdpAnswer);
		});

		webRtcEndpoint.gatherCandidates(function(error) {
			if (error) {
				stop(sessionId);
				return callback(error);
			}
		});
	});
}


function startViewer(sessionId, ws, sdpOffer, callback) {
	if (presenter.pipeline === null) {
		getKurentoClient(function(error, kurentoClient) {
			if (presenter.pipeline === null) {
				kurentoClient.create('MediaPipeline', function(error, _pipeline) {
					if (error) {
						return callback(error);
					}
					presenter.pipeline = _pipeline;

                    //presenter.pipeline.create('GStreamerFilter', {command:"textoverlay"}, function(error, _gstoverlay) {
                    //    if (error) {
                    //        return callback(error);
                    //    }
                    //});

					//presenter.pipeline.create('TextOverlay', function(error, _textoverlay) {
					//	if (error) {
					//		console.log(error);
					//		return callback(error);
					//	}
					//	var style = {text: '`                                                  I am '+sessionId+'                                                     `', 'font-desc': 'sans bold 24', deltay:30};
					//	_textoverlay.setStyle(JSON.stringify(style));
					//	console.log("TextOverlay:" + JSON.stringify(_textoverlay));
					//});

					presenter.pipeline.create('StyleComposite', function(error, _composite) {
						if (error) {
							return callback(error);
						}
						console.log(_composite)
//						var style = {width:1280, height:768, 'pad-x': 140, 'pad-y': 140, background:"http://placeimg.com/1280/768/any.jpg"};
//						var style = {width:800, height:600, 'pad-x': 140, 'pad-y': 140, background:"http://placeimg.com/800/600/any.jpg", views:[{width:400, height:500, text:"123"},{width:400, height:500, text:"1234"},{id:3},{text:"abc"}]};
						var style = {width:1280, height:720, 'pad-x': 120, 'pad-y': 120, views:[{id:1234, width:640, height:480, text:"Host Kurento"},{text:"Guest: Tao"},{text:"Position3"},{text:"Guest: Alex"}]};
						_composite.setStyle(JSON.stringify(style));
						_composite.getStyle(function(err, ret) {
							console.log( "getStyle return:" + ret );
						});
						presenter.composite = _composite;
						addViewer(sessionId, ws, sdpOffer, callback);
					});
				});
			}
		});
//	stop(sessionId);
//	return callback(noPresenterMessage);
	} else {
//	presenter.composite.setBackgroundImage("/etc/kurento/bg.jpg");
//		presenter.composite.setBackgroundImage("http://placeimg.com/800/600/any.jpg");
		addViewer(sessionId, ws, sdpOffer, callback);
	}
}

function addViewer(sessionId, ws, sdpOffer, callback) {
	clearCandidatesQueue(sessionId);

	if (presenter === null) {
		stop(sessionId);
		return callback(noPresenterMessage);
	}

	presenter.pipeline.create('WebRtcEndpoint', function(error, webRtcEndpoint) {
		if (error) {
			stop(sessionId);
			return callback(error);
		}
		viewers[sessionId] = {
			"webRtcEndpoint" : webRtcEndpoint,
			"hubPort" : null,
			"ws" : ws
		}

		if (presenter === null) {
			stop(sessionId);
			return callback(noPresenterMessage);
		}

        presenter.composite.createHubPort(function (error, _hubPort) {
            if (error) {
                console.log("error creating hubport for participant");
                return calback(error);
            }
            viewers[sessionId].hubPort = _hubPort;
            _hubPort.connect(webRtcEndpoint, function() {
                console.log("connect to hub port");
//				presenter.composite.setStyle("{'background':'http://placeimg.com/640/480/any.jpg'}");
            });
        });

        //presenter.pipeline.create('TextOverlay', function(error, _textoverlay) {
        //    if (error) {
        //        return callback(error);
        //    }
        //    console.log("TextOverlay:" + JSON.stringify(_textoverlay) );
        //
        //    presenter.composite.createHubPort(function (error, _hubPort) {
        //        if (error) {
        //            console.log("error creating hubport for participant");
        //            return calback(error);
        //        }
        //        viewers[sessionId].hubPort = _hubPort;
        //        console.log("create hub port");
        //        console.log(_hubPort);
        //        _hubPort.connect(_textoverlay, function() {
        //            _textoverlay.connect(webRtcEndpoint, function() {
        //                console.log("connect to hub port");
        //                webRtcEndpoint.getStats("VIDEO", function (arg1, arg2) {
        //                    console.log(arg1);
        //                    console.log(arg2);
        //                });
        //            });
        //
        //        });
        //    });
        //});
        //

		if (candidatesQueue[sessionId]) {
			while(candidatesQueue[sessionId].length) {
				var candidate = candidatesQueue[sessionId].shift();
				webRtcEndpoint.addIceCandidate(candidate);
			}
		}

		webRtcEndpoint.on('OnIceCandidate', function(event) {
			var candidate = kurento.register.complexTypes.IceCandidate(event.candidate);
			ws.send(JSON.stringify({
				id : 'iceCandidate',
				candidate : candidate
			}));
		});

		webRtcEndpoint.processOffer(sdpOffer, function(error, sdpAnswer) {
			if (error) {
				stop(sessionId);
				return callback(error);
			}
			if (presenter === null) {
				stop(sessionId);
				return callback(noPresenterMessage);
			}

			callback(null, sdpAnswer);
			webRtcEndpoint.gatherCandidates(function(error) {
				if (error) {
					stop(sessionId);
					return callback(error);
				}
			});
		});
	});
}

function clearCandidatesQueue(sessionId) {
	if (candidatesQueue[sessionId]) {
		delete candidatesQueue[sessionId];
	}
}

function stop(sessionId) {
	if (presenter !== null && presenter.webRtcEndpoint[sessionId]) {
		//presenter.webRtcEndpoint[sessionId].disconnect(presenter.hubPorts[sessionId], "VIDEO");
		presenter.hubPorts[sessionId].release();
		presenter.webRtcEndpoint[sessionId].release();
		delete presenter.webRtcEndpoint[sessionId];
		//var style = {width:800, height:600, 'pad-y':10, background:"http://placeimg.com/1280/960/any.jpg", views:[{id:1234, width:800, height:600, text:"Host Kurento"}]};
		presenter.composite.setStyle(JSON.stringify(style));
		console.log("setStyle" + JSON.stringify(style));
                presenter.composite.getStyle(function(err, ret) {
                     console.log( "getStyle return:" + ret );
                });


	} else if (viewers[sessionId]) {
		viewers[sessionId].hubPort.release();
		viewers[sessionId].webRtcEndpoint.release();
		delete viewers[sessionId];
	}

	clearCandidatesQueue(sessionId);
}

function onIceCandidate(sessionId, _candidate) {
	var candidate = kurento.register.complexTypes.IceCandidate(_candidate);

	if (presenter.webRtcEndpoint[sessionId]) {
		//console.info('Sending presenter candidate');
		presenter.webRtcEndpoint[sessionId].addIceCandidate(candidate);
	}
	else if (viewers[sessionId] && viewers[sessionId].webRtcEndpoint) {
		//console.info('Sending viewer candidate');
		viewers[sessionId].webRtcEndpoint.addIceCandidate(candidate);
	}
	else {
		//console.info('Queueing candidate');
		if (!candidatesQueue[sessionId]) {
			candidatesQueue[sessionId] = [];
		}
		candidatesQueue[sessionId].push(candidate);
	}
}

app.use(express.static(path.join(__dirname, 'static')));
