// BFF interpreter (no-heads variant), a direct port of soup.c's evaluate().
// step() executes one instruction so the page can animate execution.
(function (root) {
  const TT = 128;
  const L = 91, R = 93; // '[' ']'
  function makeState(tape) {
    return { tape, pos: 0, h0: 0, h1: 0, steps: 0, ops: 0, halted: false };
  }
  // Returns false once the program has halted.
  function step(s) {
    if (s.halted) return false;
    const t = s.tape;
    s.h0 &= TT - 1; s.h1 &= TT - 1;
    let executed = true;
    switch (t[s.pos]) {
      case 60: s.h0--; break;            // <
      case 62: s.h0++; break;            // >
      case 123: s.h1--; break;           // {
      case 125: s.h1++; break;           // }
      case 43: t[s.h0] = (t[s.h0] + 1) & 255; break; // +
      case 45: t[s.h0] = (t[s.h0] + 255) & 255; break; // -
      case 46: t[s.h1] = t[s.h0]; break; // .
      case 44: t[s.h0] = t[s.h1]; break; // ,
      case L:
        if (t[s.h0] === 0) {
          let d = 1; s.pos++;
          for (; s.pos < TT && d > 0; s.pos++) { if (t[s.pos] === R) d--; if (t[s.pos] === L) d++; }
          s.pos--; if (d !== 0) s.pos = TT;
        }
        break;
      case R:
        if (t[s.h0] !== 0) {
          let d = 1; s.pos--;
          for (; s.pos >= 0 && d > 0; s.pos--) { if (t[s.pos] === R) d++; if (t[s.pos] === L) d--; }
          s.pos++; if (d !== 0) s.pos = -1;
        }
        break;
      default: executed = false;
    }
    s.steps++; if (executed) s.ops++;
    if (s.pos < 0) { s.halted = true; return false; }
    s.pos++;
    if (s.pos >= TT || s.steps >= 8192) { s.halted = true; s.h0 &= TT - 1; s.h1 &= TT - 1; return false; }
    return true;
  }
  function run(tape, maxSteps = 8192) {
    const s = makeState(tape);
    while (s.steps < maxSteps && step(s));
    return s.ops;
  }
  const api = { TT, makeState, step, run };
  if (typeof module !== 'undefined' && module.exports) module.exports = api; else root.BFF = api;
})(this);
