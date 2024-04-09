#ifndef MYSQL_STREAM_MANAGER_H
#define MYSQL_STREAM_MANAGER_H

#include <map>
#include <set>
#include <pcap.h>

#include "mysql_stream.h"
#include "mysql_packet.h"

struct param_info
{
    u_int n_slow_queries;
    u_int ethernet_header_size;
};

class Mysql_stream_manager
{
public:
    u_int mysql_ip;
    u_int mysql_port;
    std::map<u_longlong, Mysql_stream*> lookup;
    std::set<Mysql_query_packet*, Mysql_query_packet_time_cmp> slow_queries;
    param_info* info;

    Mysql_stream_manager(u_int mysql_ip, u_int mysql_port, param_info* info) : mysql_ip(mysql_ip), mysql_port(mysql_port),
        info(info){}
    ~Mysql_stream_manager() { cleanup();}

    static u_longlong get_key(u_int dst_ip, u_int dst_port)
    {
        return (((u_longlong)dst_ip) << 32) + (u_longlong)dst_port;
    }

    void process_pkt(const struct pcap_pkthdr* header, const u_char* packet);
    void register_query(Mysql_query_packet* query);
    void print_slow_queries();
    void cleanup();
};

#endif
