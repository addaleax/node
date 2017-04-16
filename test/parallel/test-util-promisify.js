'use strict';
const common = require('../common');
const assert = require('assert');
const fs = require('fs');
const vm = require('vm');
const { promisify } = require('util');

// Crash the process on unhandled rejections.
process.on('unhandledRejection', (err) => setImmediate(() => { throw err; }));

const stat = promisify(fs.stat);

{
  const promise = stat(__filename);
  assert(promise instanceof Promise);
  promise.then(common.mustCall((value) => {
    assert.deepStrictEqual(value, fs.statSync(__filename));
  }));
}

{
  const promise = stat('/dontexist');
  promise.catch(common.mustCall((error) => {
    assert.strictEqual(error.message,
                       "ENOENT: no such file or directory, stat '/dontexist'");
  }));
}

{
  function fn() {}
  function promisifedFn() {}
  fn[promisify.custom] = promisifedFn;
  assert.strictEqual(promisify(fn), promisifedFn);
}

{
  async function fn() {}
  assert.strictEqual(promisify(fn), fn);
}

{
  const fn = vm.runInNewContext('(function() {})');
  assert.notStrictEqual(Object.getPrototypeOf(promisify(fn)),
                        Function.prototype);
}

{
  function fn(callback) {
    callback(null, 'foo', 'bar');
  }
  promisify(fn)().then(common.mustCall((value) => {
    assert.deepStrictEqual(value, ['foo', 'bar']);
  }));
}
