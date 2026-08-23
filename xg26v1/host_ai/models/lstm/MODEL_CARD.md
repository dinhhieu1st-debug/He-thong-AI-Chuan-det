# LSTM model card

## Status

Research prototype sequence model. It is exported for Raspberry Pi demo testing,
but is not validated for clinical deployment.

## Input and output

- Input tensor: `(batch, 20, 3)`.
- Channels per drop: `ratio`, `error_percent`, `delta_ratio`.
- Output logits: class `1 NORMAL`, `2 ATTENTION`, `3 WARNING`.
- Training source: synthetic v1 only.

Normalization parameters are fit on the training split only and stored in
`inference_config.json`.

## Architecture

- One LSTM layer, hidden size 32.
- Dense layer 16 with ReLU and dropout 0.10.
- Three output logits.
- Balanced cross-entropy class weights.
- Seed: 20260813.
- Best epoch: 28, selected by validation macro F1.

## Independent test results

- Samples: 7,488 from 48 held-out sessions.
- Accuracy: 0.9112.
- Balanced accuracy: 0.8941.
- Macro F1: 0.8840.
- NORMAL recall: 0.9290.
- ATTENTION recall: 0.8454.
- WARNING recall: 0.9080.
- WARNING false-alarm rate on non-WARNING samples: 0.0119.
- Any-alert false-alarm rate on NORMAL samples: 0.0710.

Confusion matrix (rows actual, columns predicted; order 1, 2, 3):

```text
[[4543, 341,    6],
 [ 128,1066,   67],
 [  68,  55, 1214]]
```

## Export verification

- ONNX structural checker: PASS.
- ONNX and PyTorch predictions match on 64 test samples.
- Maximum absolute logit difference: 0.0000011921.
- ONNX size: about 23 KB.

## Comparison and limitations

The MLP baseline scored 0.9196 test accuracy and 0.9147 WARNING recall, slightly
above this LSTM. A paired comparison found 231 samples correct only by MLP versus
168 correct only by LSTM. Therefore the Pi demo should initially run both models
in shadow mode rather than treating the LSTM as proven superior.

All performance is on synthetic held-out sessions. Irregular scenarios remain the
weakest group. A separate elapsed-time watchdog is still required when no new drop
arrives.
