/**
 * Minimal PCAP inspector to verify packet sizes/ports before full decoding.
 */

#include <pcap.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>

namespace {

struct Parsed {
    uint16_t eth_type = 0;
    uint8_t ip_proto = 0;
    uint16_t src_port = 0;
    uint16_t dst_port = 0;
    size_t payload_len = 0;
    bool udp = false;
};

bool parse_packet(const pcap_pkthdr* hdr, const u_char* data, Parsed& out) {
    if (!hdr || !data) return false;
    if (hdr->caplen < 14 + 20) return false;  // eth + minimal IPv4

    // Ethernet
    out.eth_type = (data[12] << 8) | data[13];
    if (out.eth_type != 0x0800) return false;  // only IPv4 here

    // IPv4 header
    const u_char* ip = data + 14;
    size_t ip_header_len = (ip[0] & 0x0F) * 4;
    if (ip_header_len < 20 || hdr->caplen < 14 + ip_header_len + 8) return false;

    out.ip_proto = ip[9];
    if (out.ip_proto != 17) return true;  // non-UDP: still count, but no ports

    // UDP
    out.udp = true;
    const u_char* udp = ip + ip_header_len;
    out.src_port = (udp[0] << 8) | udp[1];
    out.dst_port = (udp[2] << 8) | udp[3];

    size_t payload_offset = 14 + ip_header_len + 8;
    if (hdr->caplen < payload_offset) return false;
    out.payload_len = hdr->caplen - payload_offset;
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: pcap_debug_dump <pcap_file> [max_print=10]\n";
        return 1;
    }
    std::string path = argv[1];
    int max_print = (argc > 2) ? std::atoi(argv[2]) : 10;

    char errbuf[PCAP_ERRBUF_SIZE] = {0};
    pcap_t* handle = pcap_open_offline(path.c_str(), errbuf);
    if (!handle) {
        std::cerr << "pcap_open_offline failed: " << errbuf << "\n";
        return 2;
    }

    int linktype = pcap_datalink(handle);
    std::cout << "Linktype: " << linktype << "\n";
    if (linktype != DLT_EN10MB) {
        std::cout << "Expected Ethernet (DLT_EN10MB)\n";
    }

    size_t total = 0;
    std::map<size_t, size_t> payload_hist;
    pcap_pkthdr* hdr = nullptr;
    const u_char* data = nullptr;

    while (true) {
        int rc = pcap_next_ex(handle, &hdr, &data);
        if (rc == 1) {
            total++;
            Parsed p{};
            if (parse_packet(hdr, data, p)) {
                if (p.udp) {
                    payload_hist[p.payload_len]++;
                    if (max_print > 0) {
                        std::cout << "pkt " << total << " len " << hdr->caplen
                                  << " payload " << p.payload_len << " ports "
                                  << p.src_port << "->" << p.dst_port << "\n";
                        max_print--;
                    }
                }
            }
        } else if (rc == -1) {
            std::cerr << "pcap_next_ex error: " << pcap_geterr(handle) << "\n";
            break;
        } else {
            break;  // 0 timeout (offline shouldn't) or -2 EOF
        }
    }

    std::cout << "Total packets: " << total << "\n";
    std::cout << "UDP payload size histogram (first 10 entries):\n";
    int printed = 0;
    for (const auto& kv : payload_hist) {
        std::cout << "  " << kv.first << " bytes: " << kv.second << "\n";
        if (++printed >= 10) break;
    }

    pcap_close(handle);
    return 0;
}
