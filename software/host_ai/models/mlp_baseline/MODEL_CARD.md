# MLP baseline model card

## Status

Research prototype baseline. This model is not validated for clinical use and is
not yet approved for Raspberry Pi deployment.

## Input and output

- Input: 60 flattened values representing 20 past drops x 3 features
  (`ratio`, `error_percent`, `delta_ratio`).
- Output classes: `1 NORMAL`, `2 ATTENTION`, `3 WARNING`.
- Training source: synthetic v1 only.

## Fixed architecture

- StandardScaler
- MLP hidden layers: 64, 32
- ReLU activation
- Balanced sample weights
- Seed: 20260813

## Independent test results

- Samples: 7,488 from 48 held-out sessions
- Accuracy: 0.9196
- Balanced accuracy: 0.9048
- Macro F1: 0.8947
- NORMAL recall: 0.9352
- ATTENTION recall: 0.8644
- WARNING recall: 0.9147
- WARNING false-alarm rate on non-WARNING samples: 0.0102
- Any-alert false-alarm rate on NORMAL samples: 0.0648

Confusion matrix (rows are actual, columns predicted; order 1, 2, 3):

```text
[[4573, 310,    7],
 [ 115,1090,   56],
 [  58,  56, 1223]]
```

## Known limitations

- Performance is measured on synthetic held-out sessions, not independent real
  clinical data.
- `irregular` sessions are the weakest group (roughly 49%-60% accuracy depending
  on preset).
- Training reached the configured 120-iteration cap while loss was still slowly
  decreasing; the result remains a comparison baseline.
- A no-drop watchdog must run alongside AI because a completely absent next drop
  produces no new event for an event-driven model.
- Compare with an LSTM before choosing the deployment model.
