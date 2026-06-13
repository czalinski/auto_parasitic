#!/usr/bin/env python3
import time
import csv
import os
from datetime import datetime
import socket

import ADS1263
import RPi.GPIO as GPIO

# -------- CONFIG --------
SAMPLE_INTERVAL = 10.0

ESP32_BT_MAC = "AA:BB:CC:DD:EE:FF"  # replace with your ESP32's Bluetooth MAC address

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

# -------- INIT ADC --------
GPIO.setmode(GPIO.BCM)

adc = ADS1263.ADS1263()
adc.ADS1263_init_ADC1(vref='ADS1263_2_5V', rate='ADS1263_100SPS')
adc.ADS1263_SetMode(1)   # differential mode

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

def sync_time_to_esp32():
    sock = None
    try:
        sock = socket.socket(socket.AF_BLUETOOTH, socket.SOCK_STREAM, socket.BTPROTO_RFCOMM)
        sock.settimeout(10.0)
        sock.connect((ESP32_BT_MAC, 1))
        sock.sendall(f"{int(time.time())}\n".encode())
        resp = sock.recv(16).decode().strip()
        if resp == "OK":
            print("[BT] Time sync acknowledged by ESP32")
        else:
            print(f"[BT] Unexpected response: {resp!r}")
    except Exception as e:
        print(f"[BT] Time sync failed: {e}")
    finally:
        if sock:
            sock.close()


def read_channel(idx):
    adc.ADS1263_SetDiffChannal(idx)
    adc.ADS1263_WaitDRDY()
    return adc.ADS1263_Read_ADC_Data() * 2.5 / 0x7FFFFFFF

# -------- STARTUP --------
sync_time_to_esp32()

# -------- MAIN LOOP --------
try:
    while True:

        # ---- HOURLY ROTATION ----
        if datetime.now().strftime("%Y%m%d_%H") != current_hour:
            print("[INFO] Hour changed — rotating log file")
            start_new_log_file()

        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        raw_volt = read_channel(VOLTAGE_CHANNEL)
        battery_voltage = abs(raw_volt) * DIVIDER_RATIO

        raw_shunt = read_channel(SHUNT_CHANNEL)
        current_A = abs(raw_shunt) / SHUNT_RESISTANCE

        row = [timestamp, raw_volt, battery_voltage, raw_shunt, current_A]
        print("ROW:", row)

        with open(log_path, "a", newline="") as f:
            csv.writer(f).writerow(row)

        time.sleep(SAMPLE_INTERVAL)

except KeyboardInterrupt:
    print("[INFO] Keyboard interrupt — exiting")

finally:
    print("[INFO] Cleaning up GPIO and ADC")
    adc.ADS1263_Exit()
    GPIO.cleanup()
