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
var SERVERURLS  = require('./ServerURLS.js');

var argv = minimist(process.argv.slice(2), {
        default: SERVERURLS
});

var app = express();

/*
 * Definition of global variables.
 */
var idCounter = 0;
var candidatesQueue = {};
var kurentoClient = null;
var presenter = {
	pipeline : null,
	composite: null,
	webRtcEndpoint : {},
	hubPorts : {}
};

var viewers = [];
var noPresenterMessage = 'No active presenter. Try again later...';

/*
 * Server startup
 */
var asUrl = url.parse(argv.as_uri);
var port = asUrl.port;
var server = app.listen(port, function() {
    console.log('Kurento Tutorial started - Composite Example.');
    console.log('Open ' + url.format(asUrl) + ' with a WebRTC capable browser');
    console.log('Using ' + url.format(argv.ws_uri) + ' as backend Kurento Server.');
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
	console.log('Connection received with sessionId ' + sessionId);

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
        console.log('Connection ' + sessionId + ' received message ', message);

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
				
				presenter.pipeline.create('Composite', function(error, _composite) {
					if (error) {
						return callback(error);
					}
					presenter.composite = _composite;
					addPresenter(sessionId, ws, sdpOffer, callback);
				});
			});
		} else {
			addPresenter(sessionId, ws, sdpOffer, callback);
		}
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
            webRtcEndpoint.connect(_hubPort);
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
	clearCandidatesQueue(sessionId);

	if (presenter === null) {
		stop(sessionId);
		return callback(noPresenterMessage);
	}

	console.warn("startViewer sid=" + sessionId);
	presenter.pipeline.create('WebRtcEndpoint', function(error, _webRtcEndpoint) {
		if (error) {
			stop(sessionId);
			return callback(error);
		}
		viewers[sessionId] = {
			"webRtcEndpoint" : _webRtcEndpoint,
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
			console.log("create hub port");
            _hubPort.connect(_webRtcEndpoint);
		});


		if (candidatesQueue[sessionId]) {
			while(candidatesQueue[sessionId].length) {
				var candidate = candidatesQueue[sessionId].shift();
				_webRtcEndpoint.addIceCandidate(candidate);
			}
		}

        _webRtcEndpoint.on('OnIceCandidate', function(event) {
            var candidate = kurento.register.complexTypes.IceCandidate(event.candidate);
            ws.send(JSON.stringify({
                id : 'iceCandidate',
                candidate : candidate
            }));
        });

		_webRtcEndpoint.processOffer(sdpOffer, function(error, sdpAnswer) {
			if (error) {
				stop(sessionId);
				return callback(error);
			}
			if (presenter === null) {
				stop(sessionId);
				return callback(noPresenterMessage);
			}


			callback(null, sdpAnswer);
		    _webRtcEndpoint.gatherCandidates(function(error) {
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
		presenter.hubPorts[sessionId].release();
		presenter.webRtcEndpoint[sessionId].release();
		delete presenter.webRtcEndpoint[sessionId];

	} else if (viewers[sessionId]) {
		viewers[sessionId].hubPort.release();
		viewers[sessionId].webRtcEndpoint.release();
		delete viewers[sessionId];
	}

	clearCandidatesQueue(sessionId);
}

function onIceCandidate(sessionId, _candidate) {
    var candidate = kurento.register.complexTypes.IceCandidate(_candidate);
	// console.log(sessionId);
	// console.log(viewers[sessionId]);
	// if (viewers[sessionId]) console.log( viewers[sessionId].webRtcEndpoint);

    if (presenter.webRtcEndpoint[sessionId]) {
        console.info('Sending presenter candidate');
        presenter.webRtcEndpoint[sessionId].addIceCandidate(candidate);
    }
    else if (viewers[sessionId] && viewers[sessionId].webRtcEndpoint) {
        console.info('Sending viewer candidate');
        viewers[sessionId].webRtcEndpoint.addIceCandidate(candidate);
    }
    else {
        console.info('Queueing candidate');
        if (!candidatesQueue[sessionId]) {
            candidatesQueue[sessionId] = [];
        }
        candidatesQueue[sessionId].push(candidate);
    }
}

app.use(express.static(path.join(__dirname, 'static')));
