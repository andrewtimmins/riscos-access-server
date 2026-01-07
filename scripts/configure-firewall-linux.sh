#!/bin/bash
# RISC OS Access Server - Firewall Configuration Check
# Adds rules for ports 32770, 32771, and 49171 (UDP)

echo "Configuring firewall for RISC OS Access Server..."

PORTS="32770 32771 49171"

# Check for UFW (Ubuntu/Debian/Mint)
if command -v ufw >/dev/null 2>&1; then
    echo "Detected UFW. Adding rules..."
    for port in $PORTS; do
        echo "Allowing $port/udp..."
        ufw allow $port/udp
    done
    echo "Done. You may need to run 'ufw reload' if it was inactive."
    exit 0
fi

# Check for Firewalld (Fedora/RHEL/CentOS)
if command -v firewall-cmd >/dev/null 2>&1; then
    echo "Detected Firewalld. Adding rules..."
    for port in $PORTS; do
        echo "Allowing $port/udp..."
        firewall-cmd --permanent --add-port=$port/udp
    done
    echo "Reloading firewall..."
    firewall-cmd --reload
    echo "Done."
    exit 0
fi

echo "Error: Neither 'ufw' nor 'firewalld' found."
echo "Please manually open UDP ports: 32770, 32771, 49171"
exit 1
