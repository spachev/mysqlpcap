#include <cstdio>
#include <cstdint>
#include <cstring>
#include <arpa/inet.h> // For ntohs (Network to Host Short)

// --- Constants for Header Sizes ---
const int ETH_HLEN_STD = 14;     // Standard Ethernet II (MACs + EtherType)
const int MIN_IP_HEADER_LEN = 20; // Minimum size of a valid IPv4 header

// --- Structs for Parsing ---

#pragma pack(push, 1)

// Placeholder for pcap_pkthdr, used to get captured length (caplen)
struct pcap_pkthdr {
    long tv_sec;
    long tv_usec;
    int caplen;  // Length of portion present
    int len;     // Length of original packet
};

// Simplified IP header structure for validation checks
struct ip_header {
    uint8_t ver_ihl;        // Version (4 bits) + IHL (4 bits)
    uint8_t tos;            // Type of Service
    uint16_t total_length;  // Total length of IP datagram
    // ... rest of header fields are not strictly needed for basic validation
};

#pragma pack(pop)

/**
 * @brief Performs basic validation of an IPv4 header at a given offset.
 *
 * Checks for IPv4 version, minimum header length (IHL), and reasonable total length
 * relative to the captured packet size.
 *
 * @param packet Pointer to the raw packet data.
 * @param eth_header_size The proposed Ethernet header size (offset to the IP header).
 * @param packet_caplen The captured length of the packet.
 * @return true if the IP header at the offset is likely valid, false otherwise.
 */
static bool is_valid_ip_header(const u_char* packet, int eth_header_size, int packet_caplen) {
    // 1. Boundary check: Ensure there is enough space for at least a minimum IP header.
    if (eth_header_size + MIN_IP_HEADER_LEN > packet_caplen) {
        return false;
    }

    const ip_header* ip = (const ip_header*)(packet + eth_header_size);

    // 2. Check IP Version (must be 4)
    uint8_t ip_version = (ip->ver_ihl >> 4) & 0x0F;
    if (ip_version != 4) {
        return false;
    }

    // 3. Check IHL (Internet Header Length): Must be at least 5 (5 * 4 = 20 bytes)
    uint8_t ip_ihl = ip->ver_ihl & 0x0F;
    if (ip_ihl < 5) {
        return false;
    }

    uint16_t ip_header_len = ip_ihl * 4;

    // 4. Check Total Length: Must be reasonable and not exceed the captured payload.
    uint16_t ip_total_length = ntohs(ip->total_length);

    // Total length must be at least the size of the IP header itself.
    if (ip_total_length < ip_header_len) {
        return false;
    }

    // The entire IP datagram length should not exceed the available captured data.
    if (ip_total_length > packet_caplen - eth_header_size) {
        return false;
    }

    // All basic structural checks passed.
    return true;
}


/**
 * @brief Iteratively determines the size of the Link-Layer (Ethernet) header by
 * validating the subsequent IP header structure.
 *
 * This function abandons simple EtherType checks and instead brute-force validates
 * offsets from 14 up to 22 bytes, returning the first offset that starts a valid
 * IPv4 header.
 *
 * @param pcap_header_ptr Pointer to the pcap packet header, used to determine captured length.
 * @param packet Pointer to the raw packet data captured by pcap.
 * @return The size of the Link-Layer header in bytes (14 to 22), or 14 as a fallback.
 */
int detect_eth_header_size(void* pcap_header_ptr, const u_char* packet) {
    // Cast the void pointer to the known pcap header type to access caplen.
    const pcap_pkthdr* header = (const pcap_pkthdr*)pcap_header_ptr;
    int packet_caplen = header->caplen;

    // We start at the minimum standard size (14) and iterate up to 22 bytes (802.3/LLC/SNAP).
    const int MIN_ETH_SIZE = 14;
    const int MAX_ETH_SIZE = 22;

    for (int current_eth_size = MIN_ETH_SIZE; current_eth_size <= MAX_ETH_SIZE; ++current_eth_size) {
        if (is_valid_ip_header(packet, current_eth_size, packet_caplen)) {
            // Success: Found a valid IP header starting at this offset.
            return current_eth_size;
        }
    }

    // If the loop finishes without finding a valid IP header up to 22 bytes,
    // we return 0 to indicate the failure
    return 0;
}


#ifdef TEST_PCAP_DETECT

void test_detection(const char* name, const u_char* packet, int packet_caplen, int expected) {
    pcap_pkthdr header;
    header.caplen = packet_caplen; // Set the captured length for validation bounds

    int size = detect_eth_header_size(&header, packet);

    printf("Test: %s\n", name);
    printf("  Packet Caplen: %d bytes\n", packet_caplen);
    printf("  Detected Size: %d bytes\n", size);
    printf("  Expected Size: %d bytes\n", expected);
    printf("  Result: %s\n", (size == expected ? "PASS" : "FAIL"));
    printf("----------------------------------------\n");
}

int main() {
    printf("Iterative Ethernet Header Size Detection with IP Validation (14-22 bytes)\n");
    printf("=========================================================================\n");

    // Base 14-byte Ethernet Header (MACs + EtherType)
    // Followed by a 20-byte minimum IPv4 header (Ver/IHL=0x45, Total Length=40)
    u_char packet_base[34] = {
        // Ethernet Header (14 bytes)
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, // Dest MAC
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, // Src MAC
        0x08, 0x00,                         // EtherType (IPv4)

        // IP Header (20 bytes - starts at offset 14)
        0x45, 0x00,                         // Ver=4, IHL=5 (20 bytes)
        0x00, 0x28,                         // Total Length: 40 bytes (0x0028)
        0x00, 0x00, 0x00, 0x00, 0x40, 0x06, // Other fields... TTL=64, Proto=6 (TCP)
        0x00, 0x00,                         // Checksum
        0x01, 0x01, 0x01, 0x01,             // Src IP
        0x02, 0x02, 0x02, 0x02              // Dest IP
    };

    // --- TEST CASE 1: Standard 14-byte Header (EtherType 0x0800) ---
    // The IP header starts at offset 14.
    test_detection("Case 1: Standard 14-byte Header (IPv4)", packet_base, sizeof(packet_base), 14);

    // --- TEST CASE 2: Non-Standard 16-byte Header (e.g., driver padding) ---
    // Simulate the IP header being pushed to offset 16 due to 2 bytes of non-standard padding
    u_char packet_16byte[36] = {
        // Ethernet Header + Padding (16 bytes)
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
        0x08, 0x00, // EtherType (Ignored by the loop, but needed for realism)
        0xFF, 0xFF, // <<< 2 Bytes of Padding (Offset 14, 15) >>>

        // IP Header (20 bytes - starts at offset 16)
        0x45, 0x00, // Ver=4, IHL=5
        0x00, 0x28, // Total Length: 40 bytes
        0x00, 0x00, 0x00, 0x00, 0x40, 0x06, 0x00, 0x00,
        0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02
    };
    test_detection("Case 2: Non-Standard 16-byte Header (Padding)", packet_16byte, sizeof(packet_16byte), 16);

    // --- TEST CASE 3: VLAN Tagged 18-byte Header ---
    // IP header starts at offset 18.
    u_char packet_18byte[38] = {
        // Ethernet Header + VLAN Tag (18 bytes)
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
        0x81, 0x00, // VLAN EtherType
        0x00, 0x01, // TCI
        0x08, 0x00, // Encapsulated IPv4 EtherType

        // IP Header (20 bytes - starts at offset 18)
        0x45, 0x00, // Ver=4, IHL=5
        0x00, 0x28, // Total Length: 40 bytes
        0x00, 0x00, 0x00, 0x00, 0x40, 0x06, 0x00, 0x00,
        0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02
    };
    test_detection("Case 3: Standard 18-byte Header (VLAN)", packet_18byte, sizeof(packet_18byte), 18);

    // --- TEST CASE 4: Header too small for IP validation (caplen = 10) ---
    // The loop will fail all validation checks because the packet is too short.
    test_detection("Case 4: Packet too short (Caplen 10)", packet_base, 10, 14);


    return 0;
}

#endif
