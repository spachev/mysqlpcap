#ifndef MYSQL_STREAM_H
#define MYSQL_STREAM_H

#include <pcap.h>
#include <mysql.h>
#include "mysql_packet.h"
#include "common.h"

#include <thread>
#include <mutex>
#include <condition_variable>

class Mysql_stream_manager;

class Mysql_stream
{
public:
    u_short src_port;
    u_int src_ip;
    u_int dst_ip;
    u_short dst_port;
    Mysql_packet* first;
    Mysql_packet* last;
    Mysql_query_packet* last_query;
    u_char pkt_hdr[4];
    u_int cur_pkt_hdr_len;

    Mysql_stream_manager* sm;
    MYSQL* con;
    std::thread* th;
    std::mutex lock;
    std::mutex eof_lock;
    std::condition_variable eof_cond;
    bool reached_eof;

    Mysql_stream(Mysql_stream_manager* sm, u_int src_ip, u_short src_port, u_int dst_ip, u_short dst_port):
    sm(sm),src_ip(src_ip),first(0),last(0),last_query(0),cur_pkt_hdr_len(0),con(0),th(0),reached_eof(0)
    {
    }

    ~Mysql_stream()
    {
        cleanup();
    }

    u_int get_cur_pkt_len() { return pkt_hdr[0] + (((u_int)pkt_hdr[1]) << 8) + (((u_int)pkt_hdr[2]) << 16);}

    // returns true if the tcp packet that was appended contained the MySQL packet
    // entirely
    bool append(struct timeval ts, const u_char* data, u_int len, bool in);
    void cleanup();
    int create_new_packet(struct timeval ts, const u_char** data, u_int* len, bool in);
    void handle_packet_complete();
    void start_replay();
    void end_replay();
    void run_replay();
    void unlink_pkt(Mysql_packet* pkt);

    bool db_connect();
    void db_close();
    bool db_query(Mysql_query_packet* query_pkt);

};
#endif
