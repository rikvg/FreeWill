const assert = require('node:assert');
const { test } = require('node:test');
const { Oracle } = require('../oracle.js');

function accuracy(seq) {
  const o = new Oracle();
  let hits = 0;
  for (const c of seq) { if (o.predict() === c) hits++; o.observe(c); }
  return hits / seq.length;
}

test('learns a strict alternation', () => {
  const seq = Array.from({ length: 200 }, (_, i) => i % 2);
  assert.ok(accuracy(seq) > 0.95);
});

test('learns a period-3 pattern', () => {
  const seq = Array.from({ length: 300 }, (_, i) => [1, 1, 0][i % 3]);
  assert.ok(accuracy(seq) > 0.9);
});

test('exploits human-like "avoid long runs" bias', () => {
  // Switch with probability 0.7, like people trying to look random.
  let x = 0, s = 42; const seq = [];
  const rand = () => (s = (s * 1103515245 + 12345) % 2147483648) / 2147483648;
  for (let i = 0; i < 2000; i++) { if (rand() < 0.7) x = 1 - x; seq.push(x); }
  assert.ok(accuracy(seq) > 0.62);
});

test('stays near chance on truly random input', () => {
  const crypto = require('node:crypto');
  const seq = Array.from(crypto.randomBytes(4000), b => b & 1);
  const a = accuracy(seq);
  assert.ok(a > 0.45 && a < 0.55, `accuracy ${a}`);
});

test('rejects invalid choices', () => {
  assert.throws(() => new Oracle().observe(2));
});
