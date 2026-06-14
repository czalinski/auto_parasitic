#!/usr/bin/env bash
# Run as root from the repo: sudo ./pi_zero/setup.sh
set -euo pipefail

if [[ $EUID -ne 0 ]]; then
    echo "Run as root: sudo $0" >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_USER="${SUDO_USER:-pi}"
RUN_HOME="$(getent passwd "$RUN_USER" | cut -d: -f6)"

echo "=== Parasitic Logger — Pi Zero Setup ==="
echo "User:       $RUN_USER"
echo "Home:       $RUN_HOME"
echo "Deploy dir: $SCRIPT_DIR"
echo ""

# ── 1. Enable SPI ────────────────────────────────────────────────────────────
echo "[1/4] Enabling SPI..."
raspi-config nonint do_spi 0
echo "      OK"

# ── 2. Persistent journal ─────────────────────────────────────────────────────
# Raspberry Pi OS ships 40-rpi-volatile-storage.conf which forces Storage=volatile.
# An admin drop-in in /etc beats vendor drop-ins in /lib.
echo "[2/4] Enabling persistent journal..."
mkdir -p /etc/systemd/journald.conf.d
cat > /etc/systemd/journald.conf.d/50-persistent.conf << 'EOF'
[Journal]
Storage=persistent
EOF
mkdir -p /var/log/journal
systemd-tmpfiles --create --prefix /var/log/journal
systemctl restart systemd-journald
echo "      OK (takes effect after reboot)"

# ── 3. Install systemd service ────────────────────────────────────────────────
# Generate service file with actual username and deploy path substituted in.
echo "[3/4] Installing parasitic.service..."
sed -e "s|hwtest|$RUN_USER|g" \
    -e "s|/home/hwtest/auto_parasitic/pi_zero|$SCRIPT_DIR|g" \
    "$SCRIPT_DIR/parasitic.service" \
    > /etc/systemd/system/parasitic.service

systemctl daemon-reload
systemctl enable parasitic.service
echo "      Installed and enabled"

# ── 4. Start service ──────────────────────────────────────────────────────────
echo "[4/4] Starting service..."
systemctl restart parasitic.service
sleep 3
STATUS="$(systemctl is-active parasitic.service)"
echo "      Status: $STATUS"

echo ""
echo "=== Setup complete ==="
echo "A reboot is required for SPI to activate."
echo "After reboot the service will start automatically."
echo ""
echo "Check BT sync status any time with:"
echo "  journalctl -u parasitic.service --no-pager | grep '\\[BT\\]'"
