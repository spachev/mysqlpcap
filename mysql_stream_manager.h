#ifndef MYSQL_STREAM_MANAGER_H
#define MYSQL_STREAM_MANAGER_H

#include <map>
#include <set>
#include <pcap.h>
#include <mysql.h>

#include "mysql_stream.h"
#include "mysql_packet.h"
#include "query_pattern.h"
#include <vector>
#include <float.h>

struct Query_pattern_stats
{
    double min_exec_time;
    double max_exec_time;
    double total_exec_time;
    size_t n_queries;

    Query_pattern_stats():min_exec_time(DBL_MAX),max_exec_time(0.0),total_exec_time(0.0),n_queries(0)
    {
    }

    void record_query(double exec_time);
};

struct Query_stats
{
    std::map<std::string, Query_pattern_stats*> lookup;
    std::mutex lock;
    double total_exec_time;
    size_t n_queries;
    Query_stats():total_exec_time(0.0), n_queries(0)
    {
    }
    void record_query(const char* lookup_key, double exec_time);
    void print();
};

struct param_info
{
    std::vector<Query_pattern*> query_patterns;
    u_int n_slow_queries;
    u_int ethernet_header_size;
    bool do_explain;
    bool do_analyze;
    bool do_run;

    param_info():n_slow_queries(0), ethernet_header_size(14), do_explain(0),
        do_analyze(0), do_run(0)
    {
    }

    void add_query_pattern(const char* arg);

    ~param_info()
    {
        for (size_t i = 0; i < query_patterns.size(); i++)
        {
            delete query_patterns[i];
        }
        query_patterns.clear();
    }
};


class Mysql_stream_manager
{
public:
    u_int mysql_ip;
    u_int mysql_port;
    std::map<u_longlong, Mysql_stream*> lookup;
    std::set<Mysql_query_packet*, Mysql_query_packet_time_cmp> slow_queries;
    param_info* info;
    MYSQL* explain_con;
    Query_stats q_stats;


    Mysql_stream_manager(u_int mysql_ip, u_int mysql_port, param_info* info) : mysql_ip(mysql_ip), mysql_port(mysql_port),
        info(info), explain_con(NULL){}
    ~Mysql_stream_manager() { cleanup();}

    static u_longlong get_key(u_int dst_ip, u_int dst_port)
    {
        return (((u_longlong)dst_ip) << 32) + (u_longlong)dst_port;
    }

    void process_pkt(const struct pcap_pkthdr* header, const u_char* packet);
    void register_query(Mysql_query_packet* query);
    void explain_query(Mysql_query_packet* query, bool analyze);
    void print_slow_queries();
    bool connect_for_explain();
    void cleanup();
    void get_query_key(char* key_buf, size_t* key_buf_len, const char* query, size_t q_len);
    void finish_replay();
};

#endif
