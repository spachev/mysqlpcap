#ifndef MYSQL_STREAM_H
#define MYSQL_STREAM_H

#include <pcap.h>
#include "mysql_packet.h"
#include "common.h"


class Mysql_stream
{
public:
    u_short src_port;
    u_int src_ip;
    u_int dst_ip;
    u_short dst_port;
    Mysql_packet* first;
    Mysql_packet* last;
    u_char pkt_hdr[4];
    u_int cur_pkt_hdr_len;

    Mysql_stream(u_int src_ip, u_short src_port, u_int dst_ip, u_short dst_port):
    src_ip(src_ip),first(0),last(0),cur_pkt_hdr_len(0)
    {
    }

    ~Mysql_stream()
    {
        cleanup();
    }

    u_int get_cur_pkt_len() { return pkt_hdr[0] + (((u_int)pkt_hdr[1]) << 8) + (((u_int)pkt_hdr[2]) << 16);}
    void append(struct timeval ts, const u_char* data, u_int len, bool in);
    void cleanup();
    int create_new_packet(struct timeval ts, const u_char** data, u_int* len, bool in);

};
#endif
