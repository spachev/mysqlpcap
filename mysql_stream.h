#ifndef MYSQL_STREAM_H
#define MYSQL_STREAM_H

#include <pcap.h>
#include <mysql.h>
#include <mysqld_error.h>
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
    u_int last_tcp_seq;
    bool last_tcp_seq_inited;

    Mysql_stream(Mysql_stream_manager* sm, u_int src_ip, u_short src_port, u_int dst_ip, u_short dst_port):
        sm(sm),src_port(src_port),src_ip(src_ip),dst_ip(dst_ip),
        dst_port(dst_port),first(0),last(0),last_query(0),cur_pkt_hdr_len(0),con(0),th(0),reached_eof(0),
        last_tcp_seq(0),last_tcp_seq_inited(false)
    {
    }

    ~Mysql_stream()
    {
        cleanup();
    }

    bool db_ensure_connected()
    {
      if (!con)
        return db_connect();
      return true;
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
    void consider_unlink_pkt(Mysql_packet* pkt, bool in_replay=false);
    void register_replay_packet(Mysql_packet* pkt);

    bool db_connect();
    void db_close();
    bool db_query(Mysql_query_packet* query_pkt);
    u_longlong get_key(Mysql_packet* pkt);
    void register_stream_end(struct timeval ts);
    void append_packet(Mysql_packet* pkt);

    bool starting_packet()
    {
        return !last || last->is_complete();
    }

    bool register_tcp_seq(u_int seq)
    {
        if (!last_tcp_seq_inited || seq - last_tcp_seq > 0) // works even if the sequence wraps
        {
            last_tcp_seq_inited = true;
            last_tcp_seq = seq;
            return true;
        }

        return false;
    }

};
#endif
