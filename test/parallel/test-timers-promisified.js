'use strict';
const common = require('../common');
const assert = require('assert');
const timers = require('timers');
const { promisify } = require('util');

/* eslint-disable no-restricted-syntax */

// Crash the process on unhandled rejections.
process.on('unhandledRejection', (err) => setImmediate(() => { throw err; }));

const setTimeout = promisify(timers.setTimeout);
const setImmediate = promisify(timers.setImmediate);

{
  const promise = setTimeout(1);
  promise.then(common.mustCall((value) => {
    assert.strictEqual(value, undefined);
  }));
  assert.strictEqual(promise.timer.constructor.name, 'Timeout');
}

{
  const promise = setTimeout(1, 'foobar');
  promise.then(common.mustCall((value) => {
    assert.strictEqual(value, 'foobar');
  }));
  assert.strictEqual(promise.timer.constructor.name, 'Timeout');
}

{
  const promise = setTimeout(1, 'foobar');
  process.nextTick(common.mustCall(() => clearTimeout(promise)));
  promise.then(common.mustCall((value) => {
    assert.strictEqual(value, undefined);
  }));
}

{
  const promise = setImmediate();
  promise.then(common.mustCall((value) => {
    assert.strictEqual(value, undefined);
  }));
  assert.strictEqual(promise.timer.constructor.name, 'Immediate');
}

{
  const promise = setImmediate('foobar');
  promise.then(common.mustCall((value) => {
    assert.strictEqual(value, 'foobar');
  }));
  assert.strictEqual(promise.timer.constructor.name, 'Immediate');
}

{
  const promise = setImmediate('foobar');
  process.nextTick(common.mustCall(() => clearImmediate(promise)));
  promise.then(common.mustCall((value) => {
    assert.strictEqual(value, undefined);
  }));
}
