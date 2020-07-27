// Flags: --expose-gc
'use strict';
const common = require('../common');
const tmpdir = require('../common/tmpdir');
const assert = require('assert');
const path = require('path');

// Regression test for https://github.com/nodejs/node-v0.x-archive/issues/814:
// Make sure that Buffers passed to fs.write() are not garbage-collected
// even when the callback is being reused.

const fs = require('fs');

tmpdir.refresh();
const filename = path.join(tmpdir.path, 'test.txt');
const fd = fs.openSync(filename, 'w');

const size = 16 * 1024;
const writes = 1000;
let done = 0;

const ondone = common.mustCall((err) => {
  assert.ifError(err);
  if (++done < writes) {
    if (done % 25 === 0) global.gc();
    setImmediate(write);
  } else {
    assert.strictEqual(
      fs.readFileSync(filename, 'utf8'),
      'x'.repeat(writes * size));
  }
}, writes);

write();
function write() {
  const buf = Buffer.alloc(size, 'x');
  fs.write(fd, buf, 0, buf.size, -1, ondone);
}
