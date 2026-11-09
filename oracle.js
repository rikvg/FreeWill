// Oracle: predicts the next binary choice from the history of past choices.
// It keeps n-gram counts for every context length 1..maxOrder and lets the
// contexts that have predicted well so far vote on the next guess.
(function (root) {
  class Oracle {
    constructor(maxOrder = 5) {
      this.maxOrder = maxOrder;
      this.history = [];
      this.counts = new Map(); // "order:context" -> [count0, count1]
      this.score = new Array(maxOrder + 1).fill(0); // running accuracy per order
    }

    _key(order, end) {
      return order + ':' + this.history.slice(end - order, end).join('');
    }

    _predictOrder(order) {
      if (this.history.length < order) return null;
      const c = this.counts.get(this._key(order, this.history.length));
      if (!c || c[0] === c[1]) return null;
      return { guess: c[1] > c[0] ? 1 : 0, confidence: Math.abs(c[1] - c[0]) / (c[0] + c[1]) };
    }

    // Returns 0 or 1.
    predict() {
      let vote = 0;
      for (let k = 0; k <= this.maxOrder; k++) {
        const p = this._predictOrder(k);
        if (!p) continue;
        const weight = (1 + Math.max(0, this.score[k])) * p.confidence;
        vote += p.guess === 1 ? weight : -weight;
      }
      if (vote === 0) return this.history.length ? 1 - this.history[this.history.length - 1] : 0;
      return vote > 0 ? 1 : 0;
    }

    // Record the player's actual choice (0 or 1).
    observe(choice) {
      if (choice !== 0 && choice !== 1) throw new Error('choice must be 0 or 1');
      for (let k = 0; k <= this.maxOrder; k++) {
        const p = this._predictOrder(k);
        if (p) this.score[k] = 0.9 * this.score[k] + (p.guess === choice ? 1 : -1);
      }
      const n = this.history.length;
      for (let k = 0; k <= this.maxOrder && k <= n; k++) {
        const key = this._key(k, n);
        const c = this.counts.get(key) || [0, 0];
        c[choice]++;
        this.counts.set(key, c);
      }
      this.history.push(choice);
    }
  }

  if (typeof module !== 'undefined' && module.exports) module.exports = { Oracle };
  else root.Oracle = Oracle;
})(this);
