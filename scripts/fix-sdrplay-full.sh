#!/bin/bash
# Full SDRplay service reset script (macOS)
# Run with: sudo scripts/fix-sdrplay-full.sh
# Add to sudoers for passwordless: see bottom of script
#
# NOTE: You may need to customize the USB hub/port numbers (lines 18-20)
# for your specific hardware setup. Use `uhubctl` to discover your layout.

set -e

echo "=== SDRplay Full Reset ==="

# 1. Stop launchd service (prevents respawning) and kill all instances
echo "[1/5] Stopping SDRplay service..."
# Unload launchd service first to prevent respawning
launchctl unload /Library/LaunchDaemons/com.sdrplay.service.plist 2>/dev/null || true
sleep 1
# Kill any remaining instances
killall -9 sdrplay_apiService 2>/dev/null || true
sleep 1
# Verify no instances running
if pgrep -x sdrplay_apiService > /dev/null; then
    echo "    WARNING: Service still running, force killing..."
    pkill -9 -x sdrplay_apiService 2>/dev/null || true
    sleep 1
fi

# 2. Power cycle SDRplay USB ports
echo "[2/5] Power cycling SDRplay USB ports..."
if command -v /opt/homebrew/bin/uhubctl &> /dev/null; then
    /opt/homebrew/bin/uhubctl -l 2-1.1.4 -p 3,4 -a off 2>/dev/null || true
    sleep 3
    /opt/homebrew/bin/uhubctl -l 2-1.1.4 -p 3,4 -a on 2>/dev/null || true
    sleep 3
else
    echo "    uhubctl not found, skipping USB power cycle"
fi

# 3. Start SDRplay service (single instance)
echo "[3/5] Starting SDRplay service..."
/Library/SDRplayAPI/3.15.1/bin/sdrplay_apiService &
disown
sleep 2

# 4. Verify service running (single instance only)
echo "[4/5] Verifying service..."
INSTANCE_COUNT=$(pgrep -x sdrplay_apiService | wc -l | tr -d ' ')
if [ "$INSTANCE_COUNT" -eq 0 ]; then
    echo "    ERROR: Service not running!"
    exit 1
elif [ "$INSTANCE_COUNT" -gt 1 ]; then
    echo "    WARNING: Multiple instances detected ($INSTANCE_COUNT), killing extras..."
    # Keep only the first PID, kill the rest
    FIRST_PID=$(pgrep -x sdrplay_apiService | head -1)
    pgrep -x sdrplay_apiService | tail -n +2 | xargs kill -9 2>/dev/null || true
    sleep 1
fi
echo "    Service running: PID $(pgrep -x sdrplay_apiService | head -1)"

# 5. Test enumeration (5s timeout)
echo "[5/5] Testing enumeration..."
/opt/homebrew/bin/SoapySDRUtil --find=sdrplay 2>&1 &
SOAPY_PID=$!
sleep 5
if kill -0 $SOAPY_PID 2>/dev/null; then
    echo "    WARNING: Enumeration timed out"
    kill -9 $SOAPY_PID 2>/dev/null
    exit 1
fi
wait $SOAPY_PID 2>/dev/null || true
echo "    Enumeration completed"

echo ""
echo "=== SDRplay Reset Complete ==="

# To add passwordless sudo, run (replace USER and PATH):
# echo 'USER ALL=(ALL) NOPASSWD: /path/to/fix-sdrplay-full.sh' | sudo tee /etc/sudoers.d/fix-sdrplay
