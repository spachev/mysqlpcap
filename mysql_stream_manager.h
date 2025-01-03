#ifndef MYSQL_STREAM_MANAGER_H
#define MYSQL_STREAM_MANAGER_H

#include <map>
#include <set>
#include <pcap.h>
#include <mysql.h>

#include "mysql_stream.h"
#include "mysql_packet.h"
#include "query_pattern.h"
#include "ip_stream.h"
#include <vector>
#include <float.h>
#include <chrono>
#include <algorithm>

struct Query_pattern_stats
{
    double min_exec_time;
    double max_exec_time;
    double total_exec_time;
    size_t n_queries;
    std::vector<double> exec_times;

    Query_pattern_stats():min_exec_time(DBL_MAX),max_exec_time(0.0),total_exec_time(0.0),n_queries(0)
    {
    }

    void record_query(double exec_time);
    void finalize();
    double get_median_exec_time()
    {
        if (exec_times.size() == 0)
            return 0.0;

        if (exec_times.size() % 2)
            return exec_times[exec_times.size() / 2];

        int pos = exec_times.size() / 2;

        return (exec_times[pos] + exec_times[pos - 1])/2.0;
    }

    double get_pct_exec_time(int pct)
    {
        int pos = exec_times.size() * pct / 100 - 1;

        if (pos <= 0)
            pos = 0;
        if (pos >= exec_times.size())
            pos = exec_times.size() - 1;

        return exec_times[pos];
    }
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

    ~Query_stats();
    void record_query(const char* lookup_key, double exec_time);
    void print(FILE* csv_fp);
    void finalize();
};

struct param_info
{
    std::vector<Query_pattern*> query_patterns;
    u_int n_slow_queries;
    u_int ethernet_header_size;
    bool do_explain;
    bool do_analyze;
    bool do_run;
    bool report_progress;
    bool assert_on_query_error;
    off_t pcap_file_size;
    bool ignore_dup_key_errors;
    const char* csv_file;

    param_info():n_slow_queries(0), ethernet_header_size(14), do_explain(0),
        do_analyze(0), do_run(0),report_progress(false),assert_on_query_error(false), pcap_file_size(0),
        ignore_dup_key_errors(false),csv_file(0)
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
    std::multiset<Mysql_query_packet*, Mysql_query_packet_time_cmp> slow_queries;
    param_info* info;
    MYSQL* explain_con;
    Query_stats q_stats;
    std::chrono::time_point<std::chrono::high_resolution_clock> replay_start_ts;
    struct timeval first_packet_ts;
    bool first_packet_ts_inited;
    int replay_fd;
    bool in_replay_write;
    IP_stream ip_stream;
    FILE* csv_fp;

    Mysql_stream_manager(u_int mysql_ip, u_int mysql_port, param_info* info) : mysql_ip(mysql_ip), mysql_port(mysql_port),
        info(info), explain_con(NULL), first_packet_ts_inited(false),
        replay_fd(-1),in_replay_write(false),csv_fp(NULL) {}
    ~Mysql_stream_manager() { cleanup();}

    static u_longlong get_key(u_int dst_ip, u_int dst_port)
    {
        return (((u_longlong)dst_ip) << 32) + (u_longlong)dst_port;
    }

    Mysql_stream* find_or_make_stream(u_longlong key, Mysql_packet* pkt);

    // returns true if the packet is essential for replay,
    // false if it can be dropped when writing out the replay file
    bool process_pkt(const struct pcap_pkthdr* header, const u_char* packet);
    void register_query(Mysql_stream* s, Mysql_query_packet* query);
    void explain_query(Mysql_query_packet* query, bool analyze);
    void print_slow_queries();
    bool connect_for_explain();
    void cleanup();
    void get_query_key(char* key_buf, size_t* key_buf_len, const char* query, size_t q_len);
    void init_replay();
    void finish_replay();
    bool init_replay_file(const char* fname);
    void process_replay_file(const char* fname);
    bool write_to_replay_file(const char* data, size_t len);
    u_longlong get_ellapsed_us();
    u_longlong get_packet_ellapsed_us(Mysql_packet* p);
    std::chrono::time_point<std::chrono::high_resolution_clock> get_scheduled_ts(Mysql_packet* p);
    void print_query_stats();
};

#endif
