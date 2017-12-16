'use strict';
require('../common');
const assert = require('assert');
const { TCP, constants: TCPConstants } = process.binding('tcp_wrap');
const TCPConnectWrap = process.binding('tcp_wrap').TCPConnectWrap;

function makeConnection() {
  const client = new TCP(TCPConstants.SOCKET);

  const req = new TCPConnectWrap();
  const err = client.connect(req, '127.0.0.1', this.address().port);
  assert.strictEqual(err, 0);

  req.oncomplete = function(status, client_, req_, readable, writable) {
    assert.strictEqual(0, status);
    assert.strictEqual(client, client_);
    assert.strictEqual(req, req_);
    assert.strictEqual(true, readable);
    assert.strictEqual(true, writable);

    const err = client.shutdown();
    assert.strictEqual(err, 0);

    client.onaftershutdown = function(status) {
      assert.strictEqual(0, status);
      assert.strictEqual(client, this);
      shutdownCount++;
      client.close();
    };
  };
}

/////

let connectCount = 0;
let endCount = 0;
let shutdownCount = 0;

const server = require('net').Server(function(s) {
  connectCount++;
  s.resume();
  s.on('end', function() {
    endCount++;
    s.destroy();
    server.close();
  });
});

server.listen(0, makeConnection);

process.on('exit', function() {
  assert.strictEqual(1, shutdownCount);
  assert.strictEqual(1, connectCount);
  assert.strictEqual(1, endCount);
});
