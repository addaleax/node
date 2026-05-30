'use strict';

const common = require('../common.js');
const ffi = require('node:ffi');
const { libraryPath, ensureFixtureLibrary } = require('./common.js');

const bench = common.createBenchmark(main, {
  n: [2e6],
}, {
  flags: ['--experimental-ffi'],
});

ensureFixtureLibrary();

const { lib, functions } = ffi.dlopen(libraryPath, {
  string_concat: { arguments: ['pointer', 'pointer'], return: 'pointer' },
  free_string: { arguments: ['pointer'], return: 'void' },
});

const { string_concat, free_string } = functions;

function main({ n }) {
  const a = 'a'.repeat(1000);
  const b = 'b'.repeat(1000);

  bench.start();
  for (let i = 0; i < n; ++i)
    free_string(string_concat(a, b));
  bench.end(n);

  lib.close();
}
