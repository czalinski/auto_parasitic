#!/usr/bin/env python3
import time
import csv
import os
from datetime import datetime
import socket
import threading

import RPi.GPIO as GPIO

# -------- CONFIG --------
SAMPLE_INTERVAL = 10.0

ESP32_BT_MAC = "CC:7B:5C:F0:BD:82"  # BT MAC = WiFi base MAC (CC:7B:5C:F0:BD:80) + 2

LOG_DIR = os.path.join(os.path.expanduser("~"), "logs")
os.makedirs(LOG_DIR, exist_ok=True)

hostname = socket.gethostname()

# ADS1263 differential channel indices:
#   0 = AIN0-AIN1  → battery voltage via 910k/120k divider
#   1 = AIN2-AIN3  → shunt current via 5x 0.1Ω in parallel
VOLTAGE_CHANNEL  = 0
SHUNT_CHANNEL    = 1

SHUNT_RESISTANCE = 0.02   # ohms (5x 0.1Ω in parallel)

R_TOP = 910_000
R_BOT = 120_000
DIVIDER_RATIO = (R_TOP + R_BOT) / R_BOT

CSV_HEADER = ["timestamp", "raw_volt_V", "battery_voltage_V", "raw_shunt_V", "current_A"]

GPIO.setmode(GPIO.BCM)

# -------- LOG FILE CREATION --------
def start_new_log_file():
    global log_path, current_hour
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    current_hour = datetime.now().strftime("%Y%m%d_%H")
    log_path = f"{LOG_DIR}/{hostname}_log_{timestamp}.csv"
    print(f"[INFO] Creating new log file: {log_path}")
    with open(log_path, "w", newline="") as f:
        csv.writer(f).writerow(CSV_HEADER)

start_new_log_file()

# -------- HELPERS --------
def bt_receiver():
    """Daemon thread: sync time to ESP32, then log its streamed CSV rows."""
    while True:
        sock = None
        try:
            sock = socket.socket(socket.AF_BLUETOOTH, socket.SOCK_STREAM, socket.BTPROTO_RFCOMM)
            sock.settimeout(15.0)
            sock.connect((ESP32_BT_MAC, 1))
            sock.sendall(f"{int(time.time())}\n".encode())

            buf = b""
            got_ok = False
            session_file = None
            sock.settimeout(60.0)  # ESP32 streams every 500ms; 60s timeout catches stalls

            while True:
                chunk = sock.recv(256)
                if not chunk:
                    break
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    text = line.decode("utf-8", errors="replace").strip()
                    if not text:
                        continue
                    if not got_ok:
                        if text == "OK":
                            got_ok = True
                            print("[BT] Time sync acknowledged by ESP32")
                            ts = datetime.now().strftime("%Y%m%d_%H%M%S")
                            session_file = f"{LOG_DIR}/{hostname}_bt_{ts}.csv"
                            print(f"[BT] Logging ESP32 stream to {session_file}")
                        # discard anything before OK
                    else:
                        # header line arrives first, then data rows — write all
                        if session_file:
                            with open(session_file, "a") as f:
                                f.write(text + "\n")

        except Exception as e:
            print(f"[BT] {e}")
        finally:
            if sock:
                try:
                    sock.close()
                except Exception:
                    pass

        time.sleep(30)  # wait before reconnect attempt


def read_channel(idx):
    adc.ADS1263_SetDiffChannal(idx)
    adc.ADS1263_WaitDRDY()
    return adc.ADS1263_Read_ADC_Data() * 2.5 / 0x7FFFFFFF

# -------- STARTUP: BT sync and streaming in background --------
threading.Thread(target=bt_receiver, daemon=True).start()

# -------- INIT ADC --------
adc = None
try:
    import ADS1263
    _adc = ADS1263.ADS1263()
    ret = _adc.ADS1263_init_ADC1(vref='ADS1263_2_5V', rate='ADS1263_100SPS')
    if ret == 0:
        _adc.ADS1263_SetMode(1)
        adc = _adc
    else:
        print("[ADC] Init returned error — hat not present?")
except Exception as e:
    print(f"[ADC] Init failed: {e} — running without ADC")

# -------- MAIN LOOP --------
try:
    while True:

        # ---- HOURLY ROTATION ----
        if datetime.now().strftime("%Y%m%d_%H") != current_hour:
            print("[INFO] Hour changed — rotating log file")
            start_new_log_file()

        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        if adc is not None:
            raw_volt = read_channel(VOLTAGE_CHANNEL)
            battery_voltage = abs(raw_volt) * DIVIDER_RATIO

            raw_shunt = read_channel(SHUNT_CHANNEL)
            current_A = abs(raw_shunt) / SHUNT_RESISTANCE

            row = [timestamp, raw_volt, battery_voltage, raw_shunt, current_A]
        else:
            row = [timestamp, None, None, None, None]

        print("ROW:", row)

        with open(log_path, "a", newline="") as f:
            csv.writer(f).writerow(row)

        time.sleep(SAMPLE_INTERVAL)

except KeyboardInterrupt:
    print("[INFO] Keyboard interrupt — exiting")

finally:
    print("[INFO] Cleaning up")
    if adc is not None:
        adc.ADS1263_Exit()
    GPIO.cleanup()
