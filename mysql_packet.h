#ifndef MYSQL_PACKET_H
#define MYSQL_PACKET_H

#include <pcap.h>
#include <chrono>
#include <stdlib.h>

class Mysql_packet
{
protected:
    u_int ref_count; // to prevent from freeing if referenced in another structer

public:
    friend class Mysql_stream;
    double exec_time;
    bool in; // in or out
    struct timeval ts;
    u_char* data;
    u_int len;
    u_int cur_len;
    Mysql_packet* next;
    Mysql_packet* prev;
    bool skip; // to deal with multi-part packets
    void cleanup();
    void init();
    void mark_ref()
    {
        assert(ref_count <= 2);
        ref_count++;

    }
    bool unmark_ref()
    {
        assert (ref_count);
        ref_count--;

        if (!ref_count)
        {
            cleanup();
            return true;
        }

        return false;
    }

    Mysql_packet(struct timeval ts, u_int len, bool in): ref_count(0),in(in),ts(ts),len(len),cur_len(0),next(0),
        prev(0), skip(false) { init(); }
    Mysql_packet():ref_count(0),data(0),len(0),cur_len(0),next(0),prev(0) {}
    ~Mysql_packet() { cleanup();}
    void append(const u_char* append_data, u_int* try_append_len);
    bool is_complete() { return len == cur_len;}
    void print();
    double ts_diff(Mysql_packet* other);
    bool is_query();
    bool is_eof();

    bool replay_write(int fd, u_longlong key);
    bool replay_read(int fd, u_longlong* key);
};

class Mysql_query_packet: public Mysql_packet
{
public:
    const char* query() { return (char*)data + 1;}
    u_int query_len() { return len - 1;}

    void print_query()
    {
        printf("# exec_time = %.6fs\n%.*s\n", exec_time, query_len(), query());
    }
};

struct Mysql_query_packet_time_cmp
{
    bool operator()(const Mysql_query_packet* p1, const Mysql_query_packet* p2)
    {
        return p1->exec_time > p2->exec_time;
    }
};

#endif
