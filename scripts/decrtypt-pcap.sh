#!/bin/bash
# -----------------------------------------------------------------------------
# MySQL TLS Traffic Decryption Script using TShark (Wireshark CLI)
# -----------------------------------------------------------------------------

# --- CONFIGURATION (MUST EDIT) ---

# 1. Path to your captured traffic file (.pcap, .pcapng)
PCAP_FILE="./mysql_traffic_encrypted.pcapng"

# 2. Path to the server's private key file
SERVER_KEY="./server.key"

# 3. MySQL Server's IP Address
SERVER_IP="139.144.7.163"

# 4. MySQL Port (usually 3306)
MYSQL_PORT="3306"

# --- EXECUTION ---

echo "Starting TShark Decryption..."
echo "Input File: $PCAP_FILE"
echo "Key File: $SERVER_KEY"
echo "Server IP: $SERVER_IP:$MYSQL_PORT"
echo "-----------------------------------------------------------------"

# Check if TShark is installed
if ! command -v tshark &> /dev/null
then
    echo "Error: TShark (Wireshark CLI) is not installed."
    echo "Please install the Wireshark package on your system."
    exit 1
fi

# Check if the necessary input files exist
if [ ! -f "$PCAP_FILE" ] || [ ! -f "$SERVER_KEY" ]; then
    echo "Error: Missing PCAP file ($PCAP_FILE) or Server Key file ($SERVER_KEY)."
    echo "Please ensure the paths above are correct."
    exit 1
fi

# The TShark command:
# -r $PCAP_FILE          : Read the input capture file
# -o "tls.keys:..."      : Set the key list preference for TLS decryption.
#                          Format: IP,Port,Protocol,KeyFile. We use 'mysql' as the protocol.
# -Y "mysql"             : Apply a display filter to show only MySQL protocol traffic (the decrypted result)
# -V                     : Show packet details in verbose mode (optional, you can change to -T fields for less output)
# -n                     : Disable network object name resolution (speeds up processing)
tshark -n -r "$PCAP_FILE" \
       -o "tls.keys: $SERVER_IP,$MYSQL_PORT,mysql,$SERVER_KEY" \
       -Y "mysql" -V

EXIT_CODE=$?

echo "-----------------------------------------------------------------"

if [ $EXIT_CODE -eq 0 ]; then
    echo "TShark finished processing. Decrypted MySQL packets are displayed above."
else
    echo "TShark encountered an error (Exit Code $EXIT_CODE)."
    echo "Possible reasons for decryption failure (if no MySQL data is shown):"
    echo "1. The traffic uses Perfect Forward Secrecy (PFS) ciphers (e.g., ECDHE, DHE)."
    echo "2. The 'server.key' is password protected (TShark may prompt for a password)."
    echo "3. TShark does not have read permissions for the key file."
fi
