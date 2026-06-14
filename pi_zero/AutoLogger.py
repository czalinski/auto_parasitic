#!/usr/bin/env python3
import time
import csv
import os
from datetime import datetime, timezone
import RPi.GPIO as GPIO

# -------- CONFIG --------
SAMPLE_INTERVAL = 10.0

LOG_DIR = os.path.join(os.path.expanduser("~"), "logs")
os.makedirs(LOG_DIR, exist_ok=True)

hostname = "pi-trunk"

# ADS1263 differential channel indices:
#   4 = AIN8-AIN9 → overall shunt current via 5x 0.1Ω in parallel
SHUNT_CHANNEL    = 4  # AIN8-AIN9

SHUNT_RESISTANCE = 0.02   # ohms (5x 0.1Ω in parallel)

CSV_HEADER = ["timestamp", "raw_shunt_V", "current_A"]

GPIO.setmode(GPIO.BCM)

# -------- LOG FILE CREATION --------
def start_new_log_file():
    global log_path, current_hour
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")
    current_hour = datetime.now(timezone.utc).strftime("%Y%m%d_%H")
    log_path = f"{LOG_DIR}/{hostname}_log_{timestamp}.csv"
    print(f"[INFO] Creating new log file: {log_path}")
    with open(log_path, "w", newline="") as f:
        csv.writer(f).writerow(CSV_HEADER)

start_new_log_file()

def read_channel(idx):
    adc.ADS1263_SetDiffChannal(idx)
    adc.ADS1263_WaitDRDY()
    return adc.ADS1263_Read_ADC_Data() * 2.5 / 0x7FFFFFFF

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
        if datetime.now(timezone.utc).strftime("%Y%m%d_%H") != current_hour:
            print("[INFO] Hour changed — rotating log file")
            start_new_log_file()

        timestamp = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")

        if adc is not None:
            raw_shunt = read_channel(SHUNT_CHANNEL)
            current_A = abs(raw_shunt) / SHUNT_RESISTANCE
            row = [timestamp, raw_shunt, current_A]
        else:
            row = [timestamp, None, None]

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
