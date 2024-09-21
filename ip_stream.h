#ifndef IP_STREAM_H
#define IP_STREAM_H

#include <sys/types.h>
#include <map>
#include "common.h"

struct IP_fragment
{
    char* data;
    u_short len;
    u_short offset;
    IP_fragment* next;
    IP_fragment* prev;
    IP_fragment(const char* data, u_short len, u_short offset);
    ~IP_fragment() { delete[] data; }
};

class Mysql_stream;

struct IP_fragment_list
{
    IP_fragment* first, *last;
};

class IP_stream
{
protected:
    std::map<u_short, IP_fragment_list*> packet_map;
    void clear_fragment_list(u_short ip_id);
    void clear_fragment_list_low(IP_fragment_list* fl);
public:
    IP_stream(){}
    ~IP_stream();
    void enqueue(const struct sniff_ip* ip_header, const char* data, u_short data_len);
    void append_remaining_packets(u_short ip_id, Mysql_stream* s, struct timeval ts, bool in);
    bool has_fragments(u_short ip_id);
    bool get_first_fragment(u_short ip_id, char** data, u_int* data_len);
};

#endif
