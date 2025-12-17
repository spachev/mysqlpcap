#!/bin/bash
# -----------------------------------------------------------------------------
# MySQL TLS Traffic Decryption Script using TShark (Wireshark CLI)
# -----------------------------------------------------------------------------

# --- ARGUMENT PARSING ---

# Define a function to display usage instructions
usage() {
    echo "Usage: $0 <PCAP_FILE> <SERVER_KEY> <SERVER_IP> [MYSQL_PORT]"
    echo ""
    echo "Arguments:"
    echo "  PCAP_FILE    : Path to your captured traffic file (.pcap, .pcapng)."
    echo "  SERVER_KEY   : Path to the server's private key file (e.g., server.key)."
    echo "  SERVER_IP    : MySQL Server's IP Address (e.g., 139.144.7.163)."
    echo "  MYSQL_PORT   : MySQL Port (Optional, defaults to 3306)."
    echo ""
    exit 1
}

# Check if mandatory arguments are provided (at least 3: PCAP, Key, IP)
if [ $# -lt 3 ]; then
    usage
fi

# Assign command line arguments to variables
PCAP_FILE="$1"
SERVER_KEY="$2"
SERVER_IP="$3"
# Set MYSQL_PORT to the 4th argument, or default to 3306
MYSQL_PORT="${4:-3306}"

# --- PRE-EXECUTION CHECKS ---

echo "Starting TShark Decryption..."
echo "Input File: $PCAP_FILE"
echo "Key File: $SERVER_KEY"
echo "Server IP: $SERVER_IP:$MYSQL_PORT"
echo "-----------------------------------------------------------------"

# Check if TShark is installed
if ! command -v tshark &> /dev/null
then
    echo "Error: TShark (Wireshark CLI) is not installed."
    echo "Please install the Wireshark package on your system (e.g., sudo apt install wireshark)."
    exit 1
fi

# Check if the necessary input files exist
if [ ! -f "$PCAP_FILE" ] || [ ! -f "$SERVER_KEY" ]; then
    echo "Error: Missing PCAP file ($PCAP_FILE) or Server Key file ($SERVER_KEY)."
    echo "Please ensure the paths provided are correct."
    exit 1
fi

# --- EXECUTION ---

# The TShark command:
# -r $PCAP_FILE          : Read the input capture file
# -o "tls.keys:..."      : Set the key list preference for TLS decryption.
#                          Format: IP,Port,Protocol,KeyFile. We use 'mysql' as the protocol.
# -Y "mysql"             : Apply a display filter to show only MySQL protocol traffic (the decrypted result)
# -V                     : Show packet details in verbose mode (optional, you can change to -T fields for less output)
# -n                     : Disable network object name resolution (speeds up processing)
WIRESHARK_LOG_LEVEL=debug tshark -n -r "$PCAP_FILE" \
       -o "ssl.keys_list: $SERVER_IP,$MYSQL_PORT,mysql,$SERVER_KEY" \
       -Y "mysql" -V

EXIT_CODE=$?

# --- CLEANUP & ERROR HANDLING ---

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
