#ifndef MYSQL_STREAM_MANAGER_H
#define MYSQL_STREAM_MANAGER_H

#include <map>
#include <pcap.h>

#include "mysql_stream.h"

class Mysql_stream_manager
{
public:
    u_int mysql_ip;
    u_int mysql_port;
    std::map<u_longlong, Mysql_stream*> lookup;

    Mysql_stream_manager(u_int mysql_ip, u_int mysql_port) : mysql_ip(mysql_ip), mysql_port(mysql_port) {}
    ~Mysql_stream_manager() { cleanup();}

    static u_longlong get_key(u_int dst_ip, u_int dst_port)
    {
        return (((u_longlong)dst_ip) << 32) + (u_longlong)dst_port;
    }

    void process_pkt(const struct pcap_pkthdr* header, const u_char* packet);
    void cleanup();
};

#endif
