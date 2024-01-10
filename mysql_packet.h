#ifndef MYSQL_PACKET_H
#define MYSQL_PACKET_H

#include <pcap.h>

class Mysql_packet
{
public:
    bool in; // in or out
    struct timeval ts;
    u_char* data;
    u_int len;
    u_int cur_len;
    Mysql_packet* next;
    void cleanup();
    void init();
    Mysql_packet(struct timeval ts, u_int len): ts(ts),len(len),cur_len(0),next(0) { init(); }
    ~Mysql_packet() { cleanup();}
    void append(const u_char* append_data, u_int* try_append_len);
    bool is_complete() { return len == cur_len;}
};
#endif
