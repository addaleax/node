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
    assert(error.message.includes('ENOENT: no such file or directory, stat'));
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

{
  function fn(callback) {
    callback(null);
  }
  promisify(fn)().then(common.mustCall((value) => {
    assert.strictEqual(value, undefined);
  }));
}

{
  function fn(callback) {
    callback();
  }
  promisify(fn)().then(common.mustCall((value) => {
    assert.strictEqual(value, undefined);
  }));
}

{
  function fn(err, val, callback) {
    callback(err, val);
  }
  promisify(fn)(null, 42).then(common.mustCall(value => {
    assert.strictEqual(value, 42);
  }))
}

{
  function fn(err, val, callback) {
    callback(err, val);
  }
  promisify(fn)(new Error('oops'), null).catch(common.mustCall(err => {
    assert.strictEqual(err.message, 'oops');
  }))
}

{
  function fn(err, val, callback) {
    callback(err, val);
  }

  (async () => {
    const value = await promisify(fn)(null, 42);
    assert.strictEqual(value, 42);
  })();
}

{
  const o = {};
  const fn = promisify(function (cb) {

    cb(null, this === o);
  });

  o.fn = fn;

  o.fn().then(common.mustCall(function (val) {
    assert(val);
  }));
}

{
  const err = new Error('Should not have called the callback with the error.');
  const stack = err.stack;

  const fn = promisify(function (cb) {
    cb(null);
    cb(err);
  });

  (async () => {
    await fn();
    await Promise.resolve();
    return assert.strictEqual(stack, err.stack);
  })();
}

{
  // Currently failing. Added comment on PR here https://github.com/nodejs/node/pull/12442/files#r112007329
  // const c = function () { };
  // const a = promisify(function () { });
  // const b = promisify(a);
  // assert.notEqual(c, a);
  // assert.strictEqual(a, b);
}

{
  let errToThrow;
  const thrower = promisify(function (a, b, c, cb) {
    errToThrow = new Error();
    throw errToThrow;
  });
  thrower(1, 2, 3).then(assert.fail).then(assert.fail, (e) => assert(e === errToThrow));
}

{
  const err = new Error();

  const a = promisify(cb => cb(err))();
  const b = promisify(() => { throw err; })();

  Promise.all([
    a.then(assert.fail, function (e) {
      assert.equal(err, e);
    }),
    b.then(assert.fail, function (e) {
      assert.equal(err, e);
    })
  ]);

}