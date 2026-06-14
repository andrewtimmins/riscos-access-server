#!/bin/bash
# ShareFS Server - Firewall Configuration Check
# Adds rules for ports 32770, 32771, and 49171 (UDP)

set -e

if [ "$(id -u)" -ne 0 ]; then
    echo "Error: This script must be run as root."
    echo "Try: sudo $0"
    exit 1
fi

echo "Configuring firewall for ShareFS Server..."

PORTS=(32770 32771 49171)
NAMES=("ShareFS Server (Freeway)" "ShareFS Server (Access+)" "ShareFS Server (ShareFS)")

# Check for UFW (Ubuntu/Debian/Mint)
if command -v ufw >/dev/null 2>&1; then
    echo "Detected UFW. Adding rules..."
    for i in "${!PORTS[@]}"; do
        port=${PORTS[$i]}
        name=${NAMES[$i]}
        echo "Allowing UDP $port ($name)..."
        ufw allow "$port/udp" comment "$name" >/dev/null 2>&1 || ufw allow "$port/udp"
    done
    echo "Done. You may need to run 'ufw reload' if it was inactive."
    exit 0
fi

# Check for Firewalld (Fedora/RHEL/CentOS)
if command -v firewall-cmd >/dev/null 2>&1; then
    echo "Detected Firewalld. Adding rules..."
    for port in "${PORTS[@]}"; do
        echo "Allowing UDP $port..."
        firewall-cmd --permanent --add-port="$port/udp"
    done
    echo "Reloading firewall..."
    firewall-cmd --reload
    echo "Done."
    exit 0
fi

echo "Error: Neither 'ufw' nor 'firewalld' found."
echo "Please manually open UDP ports: 32770, 32771, 49171"
exit 1
