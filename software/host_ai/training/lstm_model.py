"""Small sequence model shared by training and optional Python inference."""

from __future__ import annotations

import torch
from torch import nn


class DripLSTM(nn.Module):
    def __init__(self, input_size: int = 3, hidden_size: int = 32, num_classes: int = 3):
        super().__init__()
        self.lstm = nn.LSTM(input_size=input_size, hidden_size=hidden_size, batch_first=True)
        self.classifier = nn.Sequential(
            nn.Linear(hidden_size, 16),
            nn.ReLU(),
            nn.Dropout(p=0.10),
            nn.Linear(16, num_classes),
        )

    def forward(self, inputs: torch.Tensor) -> torch.Tensor:
        sequence_output, _ = self.lstm(inputs)
        return self.classifier(sequence_output[:, -1, :])
