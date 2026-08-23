"""Giao dien may tinh cho Smart IV G26 va bo AI nho giot MLP + LSTM."""

from __future__ import annotations

import queue
import threading
import time
import tkinter as tk
from collections import deque
from tkinter import messagebox, ttk

import numpy as np
import serial
from serial.tools import list_ports

from pi_demo import NumpyMLP, OnnxLSTM, DEFAULT_LSTM, DEFAULT_LSTM_CONFIG, DEFAULT_MLP, build_sequence, parse_raw_line


LEVEL_TEXT = {1: "BINH THUONG", 2: "CHU Y", 3: "CANH BAO"}
LEVEL_COLOR = {1: "#159447", 2: "#d99a00", 3: "#d82929"}
PRESETS = {"Cham - 40 giot/phut": 40, "Binh thuong - 60 giot/phut": 60, "Nhanh - 80 giot/phut": 80}
DROP_TOLERANCE_MS = 200
DROP_WARNING_MS = 800
NATURAL_DRIFT_ALPHA = 0.03
NATURAL_DRIFT_MAX_STEP_RATIO = 0.001
NATURAL_DRIFT_MAX_TOTAL_RATIO = 0.15


def ai_ratio_with_tolerance(actual_ms: float, target_ms: float) -> float:
    """Keep the real filtered ratio visible to both AI models."""
    return max(1.0, actual_ms) / target_ms


def deviation_level(error_ms: float) -> int:
    """Deterministic safety bands requested for the physical drip interval."""
    magnitude = abs(error_ms)
    if magnitude <= DROP_TOLERANCE_MS:
        return 1
    if magnitude <= DROP_WARNING_MS:
        return 2
    return 3


def recover_missed_drops(actual_ms: float, target_ms: float) -> tuple[float, int]:
    """Recover an isolated interval that is an integer multiple of the setpoint.

    The photodiode occasionally misses an edge, so two or three normal drip
    intervals arrive as one long interval. Return the per-drop interval and
    the estimated number of physical drops represented by that measurement.
    """
    for represented_drops in (2, 3):
        recovered_ms = actual_ms / represented_drops
        if abs(recovered_ms - target_ms) <= DROP_TOLERANCE_MS:
            return recovered_ms, represented_drops
    return actual_ms, 1


def follow_natural_slowing(expected_ms: float, actual_ms: float,
                           original_target_ms: int) -> float:
    """Follow only a small, gradual increase in the physical drip interval.

    Sudden changes and values outside the normal physical band are never
    learned. The expected interval can only move slower, by a bounded amount
    per drop and with a bounded total drift from the prescribed setpoint.
    """
    difference = actual_ms - expected_ms
    if difference <= 0.0 or difference > DROP_TOLERANCE_MS:
        return expected_ms
    max_step = original_target_ms * NATURAL_DRIFT_MAX_STEP_RATIO
    step = min(difference * NATURAL_DRIFT_ALPHA, max_step)
    max_expected = original_target_ms * (1.0 + NATURAL_DRIFT_MAX_TOTAL_RATIO)
    return min(expected_ms + step, max_expected)


class SmartIvApp(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("Smart IV G26 - AI nho giot")
        self.geometry("920x720")
        self.minsize(820, 620)
        self.events: queue.Queue[tuple[str, object]] = queue.Queue()
        self.connection: serial.Serial | None = None
        self.running = True
        self.target_ms: int | None = None
        self.target_dpm: int | None = None
        self.adaptive_target_ms: float | None = None
        self.pending_target_dpm: int | None = None
        self.last_drop_time: float | None = None
        self.watchdog_sent = False
        self.consecutive_recovered_intervals = 0
        self.alert_latched = False
        self.output_level = 1
        self.recovery_good_drops = 0
        self.ratios: deque[float] = deque(maxlen=20)
        self.mlp = NumpyMLP(DEFAULT_MLP)
        self.lstm = OnnxLSTM(DEFAULT_LSTM, DEFAULT_LSTM_CONFIG)
        self._build_ui()
        self.refresh_ports()
        self.after(100, self.process_events)
        self.protocol("WM_DELETE_WINDOW", self.close)

    def _build_ui(self) -> None:
        top = ttk.Frame(self, padding=10)
        top.pack(fill="x")
        ttk.Label(top, text="Cong COM:").pack(side="left")
        self.port_var = tk.StringVar()
        self.port_box = ttk.Combobox(top, textvariable=self.port_var, width=18, state="readonly")
        self.port_box.pack(side="left", padx=6)
        ttk.Button(top, text="Lam moi", command=self.refresh_ports).pack(side="left")
        self.connect_button = ttk.Button(top, text="Ket noi", command=self.toggle_connection)
        self.connect_button.pack(side="left", padx=6)
        self.connection_label = ttk.Label(top, text="Chua ket noi")
        self.connection_label.pack(side="left", padx=12)

        self.tabs = ttk.Notebook(self)
        self.tabs.pack(fill="both", expand=True, padx=10, pady=(0, 10))
        self.setup_tab = ttk.Frame(self.tabs, padding=18)
        self.monitor_tab = ttk.Frame(self.tabs, padding=18)
        self.tabs.add(self.setup_tab, text="Cai dat toc do")
        self.tabs.add(self.monitor_tab, text="Giam sat AI")

        ttk.Label(self.setup_tab, text="Cho G26 tru bi xong, sau do treo bich va chon toc do", font=("Segoe UI", 14, "bold")).pack(pady=12)
        self.ready_label = ttk.Label(self.setup_tab, text="Dang cho tin hieu AI_READY tu G26", foreground="#a06000")
        self.ready_label.pack(pady=8)
        live_frame = ttk.LabelFrame(self.setup_tab, text="Toc do dang chay truc tiep", padding=12)
        live_frame.pack(fill="x", padx=90, pady=10)
        self.setup_rate_var = tk.StringVar(value="-- giot/phut")
        ttk.Label(live_frame, textvariable=self.setup_rate_var,
                  font=("Segoe UI", 18, "bold")).pack(side="left", expand=True)
        ttk.Label(self.setup_tab,
                  text="Hay quan sat vai giot on dinh, sau do chon moc gan voi toc do thuc te.",
                  foreground="#555555").pack(pady=(0, 6))
        self.preset_var = tk.StringVar(value="Binh thuong - 60 giot/phut")
        ttk.Combobox(self.setup_tab, textvariable=self.preset_var, values=list(PRESETS), state="readonly", width=32).pack(pady=8)
        custom = ttk.Frame(self.setup_tab)
        custom.pack(pady=8)
        ttk.Label(custom, text="Hoac toc do tuy chinh (giot/phut):").pack(side="left")
        self.custom_var = tk.StringVar()
        ttk.Entry(custom, textvariable=self.custom_var, width=10).pack(side="left", padx=8)
        self.set_button = ttk.Button(self.setup_tab, text="XAC NHAN TOC DO", command=self.send_target, state="disabled")
        self.set_button.pack(pady=15)

        self.level_label = tk.Label(self.monitor_tab, text="CHO DU LIEU AI", bg="#666666", fg="white", font=("Segoe UI", 22, "bold"), pady=14)
        self.level_label.pack(fill="x")
        metrics = ttk.Frame(self.monitor_tab)
        metrics.pack(fill="x", pady=12)
        self.metric_vars = {
            "HR": tk.StringVar(value="-- BPM"),
            "SpO2": tk.StringVar(value="-- %"),
            "Can": tk.StringVar(value="-- kg"),
            "Khoang giot": tk.StringVar(value="-- giay"),
            "Moc dat": tk.StringVar(value="-- giot/phut"),
            "Toc do": tk.StringVar(value="-- giot/phut"),
            "Muc sinh hieu": tk.StringVar(value="--"),
            "Muc nho giot": tk.StringVar(value="--"),
            "HR tho": tk.StringVar(value="-- BPM"),
            "SpO2 tho": tk.StringVar(value="-- %"),
            "Hoc sinh hieu": tk.StringVar(value="0/64"),
            "HR hoc duoc": tk.StringVar(value="-- BPM"),
            "SpO2 hoc duoc": tk.StringVar(value="-- %"),
        }
        for index, (name, value) in enumerate(self.metric_vars.items()):
            card = ttk.LabelFrame(metrics, text=name, padding=10)
            card.grid(row=index // 3, column=index % 3, padx=5, pady=5, sticky="nsew")
            ttk.Label(card, textvariable=value, font=("Segoe UI", 16, "bold")).pack()
        for column in range(3):
            metrics.columnconfigure(column, weight=1)
        self.detail_var = tk.StringVar(value="AI co dung sai +/-200 ms; can 20 khoang giot de khoi dong")
        ttk.Label(self.monitor_tab, textvariable=self.detail_var, font=("Segoe UI", 12)).pack(pady=12)
        self.progress = ttk.Progressbar(self.monitor_tab, maximum=20)
        self.progress.pack(fill="x", pady=5)
        self.vitals_progress_label = ttk.Label(
            self.monitor_tab, text="Sinh hieu: dang hoc baseline 0/60")
        self.vitals_progress_label.pack(anchor="w", pady=(5, 0))
        self.vitals_progress = ttk.Progressbar(self.monitor_tab, maximum=64)
        self.vitals_progress.pack(fill="x", pady=(2, 5))
        test_frame = ttk.LabelFrame(self.monitor_tab, text="Che do gia lap sinh hieu", padding=8)
        test_frame.pack(fill="x", pady=5)
        self.fake_hr_var = tk.BooleanVar(value=False)
        self.fake_spo2_var = tk.BooleanVar(value=False)
        self.fake_vitals_var = tk.IntVar(value=0)
        ttk.Radiobutton(test_frame, text="Tat fake", value=0,
                        variable=self.fake_vitals_var,
                        command=self.toggle_fake_vitals).pack(side="left", expand=True)
        ttk.Radiobutton(test_frame, text="Fake muc 2 (lech 17%)", value=2,
                        variable=self.fake_vitals_var,
                        command=self.toggle_fake_vitals).pack(side="left", expand=True)
        ttk.Radiobutton(test_frame, text="Fake muc 3 (lech 25%)", value=3,
                        variable=self.fake_vitals_var,
                        command=self.toggle_fake_vitals).pack(side="left", expand=True)
        self.fake_status_var = tk.StringVar(value="TEST TAT - dang dung cam bien that")
        ttk.Label(test_frame, textvariable=self.fake_status_var,
                  foreground="#a06000").pack(side="left", expand=True)
        self.log = tk.Text(self.monitor_tab, height=16, state="disabled", font=("Consolas", 10))
        self.log.pack(fill="both", expand=True, pady=10)

    def refresh_ports(self) -> None:
        ports = [item.device for item in list_ports.comports()]
        self.port_box["values"] = ports
        if ports and self.port_var.get() not in ports:
            self.port_var.set(ports[0])

    def toggle_connection(self) -> None:
        if self.connection is not None:
            self.connection.close()
            self.connection = None
            self.connect_button.config(text="Ket noi")
            self.connection_label.config(text="Da ngat")
            return
        if not self.port_var.get():
            messagebox.showerror("Loi", "Khong tim thay cong COM")
            return
        try:
            self.connection = serial.Serial(self.port_var.get(), 115200, timeout=0.2, write_timeout=1)
        except Exception as exc:
            messagebox.showerror("Loi ket noi", str(exc))
            return
        self.connect_button.config(text="Ngat ket noi")
        self.connection_label.config(text=f"Da ket noi {self.port_var.get()}")
        threading.Thread(target=self.serial_worker, daemon=True).start()

    def send_line(self, value: str) -> None:
        if self.connection is not None and self.connection.is_open:
            self.connection.write((value + "\n").encode("ascii"))

    def toggle_fake(self, sensor: str) -> None:
        enabled = self.fake_hr_var.get() if sensor == "HR" else self.fake_spo2_var.get()
        if self.connection is None or not self.connection.is_open:
            if sensor == "HR":
                self.fake_hr_var.set(not enabled)
            else:
                self.fake_spo2_var.set(not enabled)
            messagebox.showwarning("Chua ket noi", "Hay ket noi G26 truoc khi bat du lieu fake")
            return
        self.send_line(f"FAKE_{sensor},{1 if enabled else 0}")

    def toggle_fake_vitals(self) -> None:
        level = self.fake_vitals_var.get()
        if self.connection is None or not self.connection.is_open:
            self.fake_vitals_var.set(0)
            messagebox.showwarning("Chua ket noi", "Hay ket noi G26 truoc khi bat du lieu fake")
            return
        self.send_line(f"FAKE_VITAL,{level}")

    def send_target(self) -> None:
        try:
            target_dpm = int(self.custom_var.get()) if self.custom_var.get().strip() else PRESETS[self.preset_var.get()]
        except ValueError:
            messagebox.showerror("Sai gia tri", "Toc do phai la so nguyen giot/phut")
            return
        if not 1 <= target_dpm <= 240:
            messagebox.showerror("Sai gia tri", "Toc do phai tu 1 den 240 giot/phut")
            return
        self.pending_target_dpm = target_dpm
        self.adaptive_target_ms = None
        self.ratios.clear()
        self.last_drop_time = None
        self.watchdog_sent = False
        self.consecutive_recovered_intervals = 0
        self.alert_latched = False
        self.output_level = 1
        self.recovery_good_drops = 0
        self.send_line(f"SET,{target_dpm}")
        self.tabs.select(self.monitor_tab)
        self.detail_var.set(f"Dang cho G26 xac nhan moc {target_dpm} giot/phut...")

    def serial_worker(self) -> None:
        while self.running and self.connection is not None and self.connection.is_open:
            try:
                raw = self.connection.readline().decode("utf-8", errors="ignore").strip()
            except Exception as exc:
                self.events.put(("error", str(exc)))
                return
            now = time.monotonic()
            if raw == "AI_READY":
                if self.target_ms is None and self.pending_target_dpm is None:
                    self.events.put(("ready", None))
                elif self.pending_target_dpm is not None:
                    # Board may have reset or the first SET may have been lost.
                    self.send_line(f"SET,{self.pending_target_dpm}")
                else:
                    # Board restarted after a previous confirmed setting.
                    self.pending_target_dpm = self.target_dpm
                    self.target_ms = None
                    self.adaptive_target_ms = None
                    self.ratios.clear()
                    self.send_line(f"SET,{self.pending_target_dpm}")
            elif raw.startswith("AI_SET_OK,"):
                self.events.put(("set_ok", raw.split(",", 1)[1]))
            elif raw.startswith("TELEMETRY,"):
                parts = raw.split(",")
                if len(parts) in (8, 10, 14, 16, 17):
                    try:
                        self.events.put(("telemetry", tuple(float(value) for value in parts[1:])))
                    except ValueError:
                        pass
            elif raw.startswith("SETUP_DROP,"):
                parts = raw.split(",")
                if len(parts) in (2, 3):
                    try:
                        # Accept the old SETUP_DROP,<ms>,<dpm> frame while
                        # moving the visible/control protocol to dpm only.
                        self.events.put(("setup_drop", float(parts[-1])))
                    except ValueError:
                        pass
            elif raw.startswith("DROP_TIMEOUT,"):
                try:
                    self.events.put(("drop_timeout", float(raw.split(",", 1)[1])))
                except ValueError:
                    pass
            elif raw.startswith("VITAL_RAW,"):
                parts = raw.split(",")
                if len(parts) == 4:
                    try:
                        self.events.put(("vital_raw", tuple(int(value) for value in parts[1:])))
                    except ValueError:
                        pass
            elif raw.startswith("FAKE_HR_OK,"):
                self.events.put(("fake_ok", ("HR", raw.endswith(",1"))))
            elif raw.startswith("FAKE_SPO2_OK,"):
                self.events.put(("fake_ok", ("SPO2", raw.endswith(",1"))))
            elif raw.startswith("FAKE_VITAL_OK,"):
                self.events.put(("fake_vital_ok", int(raw.rsplit(",", 1)[1])))
            row = parse_raw_line(raw)
            if row is not None:
                _, number, board_target_ms, actual = row
                target_changed = self.target_ms is None or (
                    board_target_ms > 0
                    and abs(float(board_target_ms) - float(self.target_ms)) >= 1.0)
                if board_target_ms > 0 and target_changed:
                    # The web can change the prescription without going through
                    # this desktop process. Treat the target echoed by G26 as
                    # authoritative and discard only the old 20-drop window.
                    self.target_ms = int(board_target_ms)
                    self.target_dpm = max(1, min(240, round(60000 / self.target_ms)))
                    self.pending_target_dpm = None
                    self.adaptive_target_ms = float(self.target_ms)
                    self.ratios.clear()
                    self.last_drop_time = None
                    self.watchdog_sent = False
                    self.consecutive_recovered_intervals = 0
                    self.alert_latched = False
                    self.output_level = 1
                    self.recovery_good_drops = 0
                    self.send_line("LEVEL,1")
                    self.events.put(("target_sync", (self.target_dpm, self.target_ms)))
                if self.target_ms is None:
                    continue
                expected_ms = self.adaptive_target_ms or float(self.target_ms)
                ai_actual, represented_drops = recover_missed_drops(actual, expected_ms)
                if represented_drops > 1:
                    self.consecutive_recovered_intervals += 1
                    if self.consecutive_recovered_intervals > 1:
                        # Repeated long intervals indicate a real slow flow,
                        # not an isolated missed photodiode edge.
                        ai_actual, represented_drops = actual, 1
                else:
                    self.consecutive_recovered_intervals = 0
                preliminary_error = ai_actual - expected_ms
                if represented_drops == 1 and deviation_level(preliminary_error) == 1:
                    expected_ms = follow_natural_slowing(
                        expected_ms, ai_actual, self.target_ms)
                    self.adaptive_target_ms = expected_ms
                ratio = ai_ratio_with_tolerance(ai_actual, expected_ms)
                raw_error = ai_actual - expected_ms
                physical_level = deviation_level(raw_error)
                self.ratios.append(ratio)
                self.last_drop_time = now
                self.watchdog_sent = False
                self.events.put(("drop", (number, actual, ai_actual, represented_drops,
                                          raw_error, ratio, len(self.ratios), expected_ms)))
                if self.alert_latched:
                    if physical_level == 1:
                        self.recovery_good_drops += 1
                    else:
                        self.recovery_good_drops = 0
                    if self.recovery_good_drops >= 3:
                        # The fault has physically cleared. Remove old fault
                        # samples so their movement through the 20-step window
                        # cannot make the displayed state oscillate.
                        self.ratios.clear()
                        self.ratios.extend([1.0] * 20)
                        self.alert_latched = False
                        self.output_level = 1
                        self.recovery_good_drops = 0
                        self.send_line("LEVEL,1")
                        self.events.put(("level", (1, "DA ON DINH 3 GIOT - XOA LICH SU SU CO")))
                        continue
                if len(self.ratios) < 20 and physical_level >= 2:
                    self.alert_latched = True
                    self.output_level = max(self.output_level, physical_level)
                    self.recovery_good_drops = 0
                    self.send_line(f"LEVEL,{self.output_level}")
                    self.events.put(("level", (self.output_level,
                        f"LECH {raw_error:+.0f} ms - MUC VAT LY {physical_level}")))
                if len(self.ratios) == 20:
                    sequence = build_sequence(self.ratios)
                    mlp_level, mlp_prob = self.mlp.predict(sequence)
                    lstm_level, lstm_prob = self.lstm.predict(sequence)
                    ai_level = max(mlp_level, lstm_level)
                    # AI remains visible as a trend analyser. The actuator
                    # follows the explicit 200/800 ms physical safety bands,
                    # preventing an error within the yellow band from being
                    # promoted to red by a mismatched research model.
                    level = physical_level
                    detail = f"MLP={LEVEL_TEXT[mlp_level]} {mlp_prob[mlp_level-1]*100:.1f}% | LSTM={LEVEL_TEXT[lstm_level]} {lstm_prob[lstm_level-1]*100:.1f}%"
                    if ai_level != physical_level:
                        detail += f" | HE THONG THEO MUC VAT LY {physical_level} ({raw_error:+.0f} ms)"
                    if level >= 2:
                        was_latched = self.alert_latched
                        self.alert_latched = True
                        if not was_latched:
                            self.recovery_good_drops = 0
                        self.output_level = max(self.output_level, level)
                    elif not self.alert_latched:
                        self.output_level = 1
                    self.send_line(f"LEVEL,{self.output_level}")
                    self.events.put(("level", (self.output_level, detail)))

    def append_log(self, text: str) -> None:
        self.log.config(state="normal")
        self.log.insert("end", text + "\n")
        self.log.see("end")
        self.log.config(state="disabled")

    def process_events(self) -> None:
        while True:
            try:
                kind, payload = self.events.get_nowait()
            except queue.Empty:
                break
            if kind == "ready":
                if self.target_ms is None:
                    self.ready_label.config(text="G26 DA TRU BI XONG - HAY CHON TOC DO", foreground="#159447")
                    self.set_button.config(state="normal")
                    self.tabs.select(self.setup_tab)
            elif kind == "set_ok":
                self.target_dpm = int(payload)
                self.target_ms = round(60000 / self.target_dpm)
                self.adaptive_target_ms = float(self.target_ms)
                self.pending_target_dpm = None
                self.tabs.select(self.monitor_tab)
                self.append_log(f"G26 da xac nhan moc {payload} giot/phut")
                self.detail_var.set(f"G26 da nhan moc {payload} giot/phut")
            elif kind == "telemetry":
                hr, oxygen, weight, interval, target, rate, level = payload[:7]
                vitals_level = int(payload[7]) if len(payload) >= 9 else 1
                drops_level = int(payload[8]) if len(payload) >= 9 else int(level)
                baseline = payload[9] if len(payload) >= 13 else 0.0
                spo2_baseline = payload[10] if len(payload) >= 13 else 0.0
                baseline_count = int(payload[11]) if len(payload) >= 13 else 0
                history_count = int(payload[12]) if len(payload) >= 13 else 0
                fake_hr = bool(int(payload[13])) if len(payload) >= 15 else False
                fake_spo2 = bool(int(payload[14])) if len(payload) >= 15 else False
                fake_vitals_level = int(payload[15]) if len(payload) >= 16 else 0
                self.fake_hr_var.set(fake_hr)
                self.fake_spo2_var.set(fake_spo2)
                self.fake_vitals_var.set(fake_vitals_level)
                active_tests = []
                if fake_hr:
                    active_tests.append("HR -35%")
                if fake_spo2:
                    active_tests.append("SpO2 -35%")
                if fake_vitals_level:
                    self.fake_status_var.set(f"DANG TEST SINH HIEU MUC {fake_vitals_level}")
                else:
                    self.fake_status_var.set(
                        "DANG TEST: " + " + ".join(active_tests)
                        if active_tests else "TEST TAT - dang dung cam bien that")
                self.metric_vars["HR"].set(f"{int(hr)} BPM" if hr > 0 else "-- BPM")
                self.metric_vars["SpO2"].set(f"{int(oxygen)} %" if oxygen > 0 else "-- %")
                if hr <= 0 or oxygen <= 0:
                    self.metric_vars["HR tho"].set("-- BPM")
                    self.metric_vars["SpO2 tho"].set("-- %")
                self.metric_vars["Can"].set(f"{weight:.3f} kg")
                self.metric_vars["Khoang giot"].set(f"{interval:.3f} giay" if interval > 0 else "-- giay")
                self.metric_vars["Moc dat"].set(f"{target:.1f} giot/phut")
                self.metric_vars["Toc do"].set(f"{rate:.1f} giot/phut" if rate > 0 else "-- giot/phut")
                self.metric_vars["Muc sinh hieu"].set(f"Muc {vitals_level}")
                self.metric_vars["Muc nho giot"].set(f"Muc {drops_level}")
                self.metric_vars["Hoc sinh hieu"].set(f"{history_count}/64")
                self.metric_vars["HR hoc duoc"].set(f"{baseline:.1f} BPM" if baseline_count else "-- BPM")
                self.metric_vars["SpO2 hoc duoc"].set(f"{spo2_baseline:.1f} %" if baseline_count else "-- %")
                self.vitals_progress["value"] = history_count
                if baseline_count < 60:
                    self.vitals_progress_label.config(
                        text=f"Dang hoc HR nen: {baseline_count}/60 | HR nen tam tinh: {baseline:.1f} BPM")
                elif history_count < 64:
                    self.vitals_progress_label.config(
                        text=f"Da hoc HR nen {baseline:.1f} BPM | Dang nap AI: {history_count}/64")
                else:
                    self.vitals_progress_label.config(
                        text=f"AI sinh hieu san sang | HR nen: {baseline:.1f} BPM")
                level = int(level)
                self.level_label.config(text=f"MUC {level} - {LEVEL_TEXT[level]}", bg=LEVEL_COLOR[level])
            elif kind == "setup_drop":
                self.setup_rate_var.set(f"{payload:.1f} giot/phut")
            elif kind == "drop_timeout":
                elapsed_ms = payload
                self.watchdog_sent = True
                self.alert_latched = True
                self.output_level = 3
                self.recovery_good_drops = 0
                self.send_line("LEVEL,3")
                detail = f"G26 XAC NHAN KHONG CO GIOT TRONG {elapsed_ms / 1000.0:.2f} GIAY"
                self.detail_var.set(f"NHO GIOT: {detail}")
                self.append_log(f"NHO GIOT: {detail}")
            elif kind == "target_sync":
                target_dpm, target_ms = payload
                self.progress["value"] = 0
                self.detail_var.set(
                    f"Web doi moc {target_dpm} giot/phut - dang hoc lai 0/20")
                self.append_log(
                    f"Dong bo moc tu G26: {target_dpm} giot/phut ({target_ms} ms/giot); "
                    "da xoa cua so nho giot cu")
            elif kind == "vital_raw":
                timestamp_ms, raw_hr, raw_spo2 = payload
                self.metric_vars["HR tho"].set(f"{raw_hr} BPM")
                self.metric_vars["SpO2 tho"].set(f"{raw_spo2} %")
                self.append_log(
                    f"SINH HIEU THO t={timestamp_ms} ms | HR={raw_hr} BPM | SpO2={raw_spo2}%"
                )
            elif kind == "fake_ok":
                sensor, enabled = payload
                state = "BAT" if enabled else "TAT"
                self.append_log(f"TEST {sensor}: {state} (ap dung sau khi hoc du 60 mau nen)")
            elif kind == "fake_vital_ok":
                self.fake_vitals_var.set(payload)
                state = "TAT" if payload == 0 else f"MUC {payload}"
                self.append_log(f"TEST SINH HIEU: {state}")
            elif kind == "drop":
                number, actual, ai_actual, represented_drops, raw_error, ratio, count, expected_ms = payload
                self.progress["value"] = count
                tolerance_text = "TRONG DUNG SAI" if abs(raw_error) <= DROP_TOLERANCE_MS else "VUOT DUNG SAI"
                recovery_text = ""
                if represented_drops > 1:
                    recovery_text = (f" | NGHI BO SOT {represented_drops - 1} GIOT: "
                                     f"AI dung {ai_actual:.0f} ms/giot")
                self.append_log(
                    f"Giot {number}: {actual} ms | lech={raw_error:+.0f} ms | "
                    f"{tolerance_text}{recovery_text} | moc dong={expected_ms:.1f} ms | "
                    f"ratio AI={ratio:.3f} | bo dem={count}/20"
                )
            elif kind == "level":
                level, detail = payload
                self.detail_var.set(f"NHO GIOT: {detail}")
                self.append_log(f"NHO GIOT: {detail}")
            elif kind == "error":
                self.connection_label.config(text=f"Loi: {payload}")
        self.after(100, self.process_events)

    def close(self) -> None:
        self.running = False
        if self.connection is not None:
            self.connection.close()
        self.destroy()


if __name__ == "__main__":
    SmartIvApp().mainloop()
