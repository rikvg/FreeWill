# FreeWill: The Oracle

Press **F** or **D** as unpredictably as you can. Before each press, the Oracle commits
to a guess about which key you'll hit. If your choices were truly free of pattern, it would
be right about 50% of the time. For most people it is right 60–70% of the time.

The idea comes from Scott Aaronson's "free will" predictor.

## Play

Open `index.html` in a browser. No build step, no dependencies, no network. You can use
the keyboard or tap the buttons.

## How it works

`oracle.js` keeps counts of what you pressed after every recent context of 0–5 presses
(for example, "after F D D you pressed F 7 times and D 2 times"). Each context length gets
a vote weighted by how confident its counts are and how well it has predicted recently.

It exploits what people actually do when they try to look random: they switch keys too
often, avoid long runs, and fall into short rhythms.

The "chance it's luck" figure is the one-sided probability of the Oracle scoring at least
that well by coin-flipping (normal approximation to the binomial).

## Test

```sh
npm test
```

The tests check that the Oracle learns repeating patterns, beats chance on a human-like
"switch 70% of the time" bias, and stays near 50% on truly random input.
