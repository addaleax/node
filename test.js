'use strict';
const { Worker } = require('worker_threads');
const w = new Worker(`
  console.log(42);
  setTimeout(() => {}, 1000);
`, { eval:true });
w.on('online', () => w.getSnapshot());
