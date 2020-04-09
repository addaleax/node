// Flags: --expose-internals
'use strict';
const common = require('../common');
const { internalBinding } = require('internal/test/binding');
const { CloneableDummy } = internalBinding('util');
const { MessageChannel, moveMessagePortToContext } = require('worker_threads');
const assert = require('assert');
const vm = require('vm');

{
  // Test 1: Clone a dummy object.
  const instance = new CloneableDummy('foo');
  const { port1, port2 } = new MessageChannel();

  assert.strictEqual(instance.get(), 'foo');
  port1.postMessage({ instance });
  assert.strictEqual(instance.get(), 'foo');

  port2.once('message', common.mustCall(({ instance }) => {
    assert.strictEqual(instance.get(), 'foo');
  }));
}

{
  // Test 2: Transfer a dummy object.
  const instance = new CloneableDummy('foo');
  const { port1, port2 } = new MessageChannel();

  assert.strictEqual(instance.get(), 'foo');
  port1.postMessage({ instance }, [instance]);
  assert.strictEqual(instance.get(), '');

  port2.once('message', common.mustCall(({ instance }) => {
    assert.strictEqual(instance.get(), 'foo');
  }));
}

{
  // Test 3: Clone a dummy object to a message port in another context.
  const instance = new CloneableDummy('foo');
  const { port1, port2 } = new MessageChannel();
  const context = vm.createContext();
  const port2Moved = moveMessagePortToContext(port2, context);
  assert(!(port2Moved instanceof Object));

  port2Moved.onmessage = common.mustCall(({ data: { instance } }) => {
    assert.strictEqual(instance.get(), 'foo');
    port1.close();
  });
  port2Moved.start();

  port1.postMessage({ instance });
}
