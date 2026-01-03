#!/bin/bash
#
# install-recovery-scripts.sh - Install SoapySDRPlay3 recovery scripts
#
# This script installs the service restart and USB reset scripts with
# proper sudoers configuration for passwordless execution.
#
# Usage:
#   sudo ./install-recovery-scripts.sh
#
# What it does:
#   1. Creates 'sdrplay' group (if not exists)
#   2. Adds current user to the group
#   3. Installs scripts to /usr/local/bin
#   4. Configures sudoers for passwordless execution
#
# After installation, log out and back in for group membership to take effect.
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Target user (the one who invoked sudo)
TARGET_USER="${SUDO_USER:-$USER}"

echo "=============================================="
echo "  SoapySDRPlay3 Recovery Scripts Installer"
echo "=============================================="
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: This script must be run with sudo${NC}"
    echo "Usage: sudo $0"
    exit 1
fi

# Detect platform
case "$(uname -s)" in
    Darwin)
        PLATFORM="macos"
        echo -e "${GREEN}Detected: macOS${NC}"
        ;;
    Linux)
        PLATFORM="linux"
        echo -e "${GREEN}Detected: Linux${NC}"
        ;;
    *)
        echo -e "${RED}Error: Unsupported platform $(uname -s)${NC}"
        exit 1
        ;;
esac

echo ""

# Step 1: Create sdrplay group
echo "Step 1: Creating 'sdrplay' group..."
if [ "$PLATFORM" = "macos" ]; then
    if dseditgroup -o read sdrplay &>/dev/null; then
        echo -e "  ${YELLOW}Group 'sdrplay' already exists${NC}"
    else
        # Find an unused GID
        GID=400
        while dscl . -list /Groups PrimaryGroupID | grep -q "\\b$GID\\b"; do
            GID=$((GID + 1))
        done
        dseditgroup -o create -i $GID sdrplay
        echo -e "  ${GREEN}Created group 'sdrplay' with GID $GID${NC}"
    fi
else
    if getent group sdrplay &>/dev/null; then
        echo -e "  ${YELLOW}Group 'sdrplay' already exists${NC}"
    else
        groupadd sdrplay
        echo -e "  ${GREEN}Created group 'sdrplay'${NC}"
    fi
fi

# Step 2: Add user to group
echo ""
echo "Step 2: Adding user '$TARGET_USER' to 'sdrplay' group..."
if [ "$PLATFORM" = "macos" ]; then
    if dseditgroup -o checkmember -m "$TARGET_USER" sdrplay &>/dev/null; then
        echo -e "  ${YELLOW}User '$TARGET_USER' is already a member${NC}"
    else
        dseditgroup -o edit -a "$TARGET_USER" -t user sdrplay
        echo -e "  ${GREEN}Added '$TARGET_USER' to 'sdrplay' group${NC}"
    fi
else
    if id -nG "$TARGET_USER" | grep -qw sdrplay; then
        echo -e "  ${YELLOW}User '$TARGET_USER' is already a member${NC}"
    else
        usermod -a -G sdrplay "$TARGET_USER"
        echo -e "  ${GREEN}Added '$TARGET_USER' to 'sdrplay' group${NC}"
    fi
fi

# Step 3: Install scripts
echo ""
echo "Step 3: Installing scripts to /usr/local/bin..."

# Ensure /usr/local/bin exists
mkdir -p /usr/local/bin

# Install sdrplay-service-restart
if [ -f "$SCRIPT_DIR/sdrplay-service-restart" ]; then
    cp "$SCRIPT_DIR/sdrplay-service-restart" /usr/local/bin/
    chmod 755 /usr/local/bin/sdrplay-service-restart
    echo -e "  ${GREEN}Installed: /usr/local/bin/sdrplay-service-restart${NC}"
else
    echo -e "  ${RED}Error: sdrplay-service-restart not found in $SCRIPT_DIR${NC}"
    exit 1
fi

# Install sdrplay-usb-reset
if [ -f "$SCRIPT_DIR/sdrplay-usb-reset" ]; then
    cp "$SCRIPT_DIR/sdrplay-usb-reset" /usr/local/bin/
    chmod 755 /usr/local/bin/sdrplay-usb-reset
    echo -e "  ${GREEN}Installed: /usr/local/bin/sdrplay-usb-reset${NC}"
else
    echo -e "  ${RED}Error: sdrplay-usb-reset not found in $SCRIPT_DIR${NC}"
    exit 1
fi

# Step 4: Configure sudoers
echo ""
echo "Step 4: Configuring sudoers..."

SUDOERS_FILE="/etc/sudoers.d/sdrplay"
SUDOERS_CONTENT="# SoapySDRPlay3 recovery scripts - passwordless sudo for sdrplay group
# Installed by install-recovery-scripts.sh

%sdrplay ALL=(ALL) NOPASSWD: /usr/local/bin/sdrplay-service-restart
%sdrplay ALL=(ALL) NOPASSWD: /usr/local/bin/sdrplay-usb-reset
"

# Write sudoers file
echo "$SUDOERS_CONTENT" > "$SUDOERS_FILE.tmp"

# Validate sudoers syntax
if visudo -c -f "$SUDOERS_FILE.tmp" &>/dev/null; then
    mv "$SUDOERS_FILE.tmp" "$SUDOERS_FILE"
    chmod 440 "$SUDOERS_FILE"
    echo -e "  ${GREEN}Installed: $SUDOERS_FILE${NC}"
else
    rm -f "$SUDOERS_FILE.tmp"
    echo -e "  ${RED}Error: Invalid sudoers syntax${NC}"
    exit 1
fi

# Step 5: Check for uhubctl (optional)
echo ""
echo "Step 5: Checking for uhubctl (optional, for USB reset)..."
if command -v uhubctl &>/dev/null; then
    echo -e "  ${GREEN}uhubctl is installed${NC}"
else
    echo -e "  ${YELLOW}uhubctl not found - USB reset will not work${NC}"
    echo "  To install uhubctl:"
    if [ "$PLATFORM" = "macos" ]; then
        echo "    brew install uhubctl"
    else
        echo "    sudo apt install uhubctl  # Debian/Ubuntu"
        echo "    sudo dnf install uhubctl  # Fedora"
    fi
fi

# Done
echo ""
echo "=============================================="
echo -e "${GREEN}Installation complete!${NC}"
echo "=============================================="
echo ""
echo "IMPORTANT: Log out and back in for group membership to take effect."
echo ""
echo "After logging back in, you can use:"
echo "  sdrplay-service-restart     # Restart SDRplay service"
echo "  sdrplay-usb-reset           # Power cycle SDRplay USB devices"
echo ""
echo "These commands will run with sudo automatically (no password required)."
echo ""

# Verify installation
echo "Verifying installation..."
if [ -x /usr/local/bin/sdrplay-service-restart ] && \
   [ -x /usr/local/bin/sdrplay-usb-reset ] && \
   [ -f /etc/sudoers.d/sdrplay ]; then
    echo -e "${GREEN}All components installed successfully!${NC}"
else
    echo -e "${RED}Some components may not have installed correctly.${NC}"
    exit 1
fi
