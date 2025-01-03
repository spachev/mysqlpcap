#include <iostream>
#include <string.h>
#include <netinet/in.h>
#include <mysql.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "common.h"
#include "mysql_stream_manager.h"




void Mysql_stream_manager::cleanup()
{
    if (csv_fp)
    {
        fclose(csv_fp);
        csv_fp = NULL;
    }

    for (std::multiset<Mysql_query_packet*, Mysql_query_packet_time_cmp>::iterator it = slow_queries.begin();
         it != slow_queries.end(); it++)
    {
        Mysql_query_packet* pkt = *it;
        pkt->unmark_ref();
        delete pkt;
    }

    slow_queries.clear();

    for (std::map<u_longlong, Mysql_stream*>::iterator it = lookup.begin(); it != lookup.end(); it++)
    {
        delete (*it).second;
    }

    lookup.clear();

    if (explain_con)
    {
        mysql_close(explain_con);
        explain_con = 0;
    }

    if (replay_fd >= 0)
    {
      close(replay_fd);
      replay_fd = -1;
    }
}

void Mysql_stream_manager::init_replay()
{
    if (info->csv_file)
    {
        csv_fp = fopen(info->csv_file, "w");
        if (!csv_fp)
            throw std::runtime_error("Could not open csv file");
    }

    replay_start_ts = std::chrono::high_resolution_clock::now();
}

u_longlong Mysql_stream_manager::get_ellapsed_us()
{
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(now - replay_start_ts).count();
}


bool Mysql_stream_manager::connect_for_explain()
{
    if (!(explain_con = mysql_init(NULL)))
    {
        fprintf(stderr, "Error initializing explain connection\n");
        return false;
    }

    if (!mysql_real_connect(explain_con, replay_host, replay_user, replay_pw, replay_db,
        replay_port, NULL, 0))
    {
        fprintf(stderr, "Error connecting for explain: %s", mysql_error(explain_con));
        mysql_close(explain_con);
        explain_con = 0;
    }

    return true;
}

void Mysql_stream_manager::explain_query(Mysql_query_packet* query, bool analyze)
{
    const char* explain_str = analyze ? "analyze format=json " : "explain ";
    u_int explain_str_len = strlen(explain_str);
    u_int q_len = query->query_len();
    u_int buf_len = query->query_len() + explain_str_len + 1;
    char* buf = new char[buf_len];
    char* p = buf;
    memcpy(p, explain_str, explain_str_len);
    p += explain_str_len;
    memcpy(p, query->query(), q_len);
    p += q_len;
    *p = 0;

    MYSQL_RES* res = 0;
    MYSQL_ROW row = 0;
    uint num_fields = 0;
    MYSQL_FIELD* fields = 0;

    if (mysql_real_query(explain_con, buf, buf_len - 1))
    {
        fprintf(stderr, "Error explaining query: %s : %s\n", buf, mysql_error(explain_con));
        goto err;
    }

    if (!(res = mysql_store_result(explain_con)))
    {
        fprintf(stderr, "Error explaining query: %s : could not store result\n", buf);
        goto err;
    }

    num_fields = mysql_num_fields(res);
    fields = mysql_fetch_fields(res);

    while ((row = mysql_fetch_row(res)))
    {
        for (uint i = 0; i < num_fields; i++)
        {
            printf("%s: %s\n", fields[i].name, row[i] ? row[i] : "NULL");
        }
    }

err:
    if (res)
        mysql_free_result(res);
    delete[] buf;
}


// Our filter ensures we get a valid TCP packet, so we skip the checks
static const struct sniff_tcp* get_tcp_header(struct param_info* info, const u_char* packet, int* tcp_header_len)
{
    int ip_header_len;
    const u_char *ip_header;
    const u_char *tcp_header;
    ip_header = packet + info->ethernet_header_size;
    ip_header_len = ((*ip_header) & 0x0F);
    ip_header_len *= 4;
    tcp_header = ip_header + ip_header_len;

    *tcp_header_len = ((*(tcp_header + 12)) & 0xF0) >> 4;
    *tcp_header_len *= 4;
    return (const struct sniff_tcp*)tcp_header;
}

static const struct sniff_ip* get_ip_header(struct param_info* info, const u_char* packet)
{
    const u_char* ip_header = packet + info->ethernet_header_size;
    return (const struct sniff_ip*)ip_header;
}

char *strncasestr(const char *haystack, const char *needle, size_t len)
{
    int i;
    size_t needle_len;

    if (0 == (needle_len = strnlen(needle, len)))
            return (char *)haystack;

    for (i=0; i<=(int)(len-needle_len); i++)
    {
            if ((tolower(haystack[0]) == tolower(needle[0])) &&
                    (0 == strncasecmp(haystack, needle, needle_len)))
                    return (char *)haystack;

            haystack++;
    }
    return NULL;
}

static bool could_be_query(const u_char* data, u_int len)
{
    const char* haystack = (char*)data + 5; // at this point could be invalid
    size_t cmp_len = len - 5;
    return len > 4 && data[4] == 0x3 && (strncasestr(haystack, "select", cmp_len) ||
        strncasestr(haystack, "update", cmp_len) ||
        strncasestr(haystack, "delete", cmp_len) ||
        strncasestr(haystack, "alter", cmp_len) ||
        strncasestr(haystack, "call", cmp_len) ||
        strncasestr(haystack, "show", cmp_len)
    );
}

bool Mysql_stream_manager::process_pkt(const struct pcap_pkthdr* header, const u_char* packet)
{
    int tcp_header_len;
    const struct sniff_tcp* tcp_header;

    const struct sniff_ip* ip_header = get_ip_header(info, packet);
    if (header->caplen < (char*)ip_header + sizeof(*ip_header) - (char*)packet ||
        ip_header->ip_p != 6 /* tcp */)
            return false;


    if (ip_header->ip_off & IP_MF)
    {
        u_short ip_header_len = ((*(char*)ip_header) & 0x0F) * 4;
        if (header->caplen < info->ethernet_header_size + ip_header_len)
            return false;
        u_short data_len = header->caplen - info->ethernet_header_size - ip_header_len;
        ip_stream.enqueue(ip_header, (char*)ip_header + ip_header_len, data_len);
        return true;
    }

    const u_char* data;
    bool has_more_fragments = false;
    u_int len;

    if (ip_stream.has_fragments(ip_header->ip_id))
    {
        u_int frag_len;
        if (!ip_stream.get_first_fragment(ip_header->ip_id, (char**)&data, &frag_len))
            assert(0); // BUG
        tcp_header = (struct sniff_tcp*)data;
        if (frag_len < sizeof(struct sniff_tcp))
            assert(0); // BUG
        tcp_header_len = (((*((char*)tcp_header + 12)) & 0xF0) >> 4) * 4;
        if (frag_len < tcp_header_len)
            return false;

        data += tcp_header_len;
        len = frag_len - tcp_header_len;
        has_more_fragments = true;
    }
    else
    {
        tcp_header = get_tcp_header(info, packet, &tcp_header_len);
        data = (const u_char*)tcp_header + tcp_header_len;

        if (header->caplen < (char*)data - (char*)packet)
            return false; //skip weird packets

        len = header->caplen - ((char*)data - (char*)packet);
    }

    bool in = (ntohl(ip_header->ip_dst.s_addr) == ntohl(mysql_ip) &&
        ntohs(tcp_header->th_dport) == mysql_port);


    if (ntohs(tcp_header->th_sport) != mysql_port && ntohs(tcp_header->th_dport) != mysql_port)
        return false;

    u_longlong key = in ? get_key(ip_header->ip_src.s_addr, tcp_header->th_sport) :
        get_key(ip_header->ip_dst.s_addr, tcp_header->th_dport);


    Mysql_stream *s;
    std::map<u_longlong, Mysql_stream*>::iterator it;

    if ((it = lookup.find(key)) == lookup.end())
    {
        if (!(tcp_header->th_flags & TH_SYN) && !in && !could_be_query(data, len))
            return false; // igore streams if we join in the middle of a conversation
        s = new Mysql_stream(this, ip_header->ip_src.s_addr, tcp_header->th_sport,
                             ip_header->ip_dst.s_addr, tcp_header->th_dport);
        lookup[key] = s;

        if (info->do_run)
            s->start_replay();
    }
    else
    {
        s = it->second;
        // TODO: this throttles the benchmark, figure out how to make it better
        if (tcp_header->th_flags & (TH_RST | TH_FIN))
        {
            s->register_stream_end(header->ts);

            if (info->do_run)
                s->end_replay();

            lookup.erase(it);
            delete s;
            return true;
        }
    }

    if (!len)
        return true;

    DEBUG_MSG("key=%llu in=%d len=%u flags=%u", key, in, len, tcp_header->th_flags);

    if (!first_packet_ts_inited)
    {
        first_packet_ts = header->ts;
        first_packet_ts_inited = true;
    }

    // for now we only filter out the retransmits
    // TODO: deal with out of order packets
    if (!s->register_tcp_seq(tcp_header->th_seq))
        return false;

    if (in && (s->starting_packet() &&  !could_be_query(data, len))) // crude hack to filter out client authentication packets
        return false;

    s->append(header->ts, data, len, in);

    if (has_more_fragments)
    {
       ip_stream.append_remaining_packets(ip_header->ip_id, s, header->ts, in);
    }

    return true; // for now
}

std::chrono::time_point<std::chrono::high_resolution_clock> Mysql_stream_manager::get_scheduled_ts(Mysql_packet* p)
{
    if (replay_speed == 0.0)
        return INVALID_TIME;

    u_longlong delta_raw = get_packet_ellapsed_us(p);
    u_longlong delta = (u_longlong)((double) delta_raw/ replay_speed);
    return replay_start_ts + std::chrono::microseconds(delta);
}

// returns false on success, true on error
bool Mysql_stream_manager::write_to_replay_file(const char* data, size_t len)
{
  return write(replay_fd, data, len) != len;
}


bool Mysql_stream_manager::init_replay_file(const char* fname)
{
  if ((replay_fd = open(fname, O_WRONLY|O_CREAT, 0660)) < 0)
    return true;

  in_replay_write = true;

  char ver_buf[2];
  int2store(ver_buf, REPLAY_FILE_VER);

  if (write_to_replay_file(REPLAY_FILE_MAGIC, REPLAY_FILE_MAGIC_LEN) ||
      write_to_replay_file(ver_buf, sizeof(ver_buf))
  )
    return true;

  return false;
}

Mysql_stream* Mysql_stream_manager::find_or_make_stream(u_longlong key, Mysql_packet* pkt)
{
  u_int src_ip = (key >> 32);
  u_int src_port = key & ((1LL << 32) - 1);

  Mysql_stream* s;
  std::map<u_longlong, Mysql_stream*>::iterator it;

  if ((it = lookup.find(key)) == lookup.end())
  {
    if (pkt->len == 0)
      return NULL; // found end of stream on an inactive  stream

    s = new Mysql_stream(this, src_ip, src_port,
                          mysql_ip, mysql_port);
    lookup[key] = s;

    if (info->do_run)
        s->start_replay();
  }
  else
  {
    s = it->second;
    // TODO: this throttles the benchmark, figure out how to make it better
    if (pkt->len == 0)
    {
        if (info->do_run)
            s->end_replay();
        lookup.erase(it);
        delete s;
        return NULL;
    }
  }

  return s;
}

void Mysql_stream_manager::process_replay_file(const char* fname)
{
  if ((replay_fd = open(fname, O_RDONLY)) < 0)
    throw std::runtime_error("Error opening replay file for reading");

  char magic[REPLAY_FILE_MAGIC_LEN];

  if (read(replay_fd, magic, REPLAY_FILE_MAGIC_LEN) != REPLAY_FILE_MAGIC_LEN)
    throw std::runtime_error("Failed to read the magic number in the replay file");

  if (memcmp(magic, REPLAY_FILE_MAGIC, REPLAY_FILE_MAGIC_LEN) != 0)
    throw std::runtime_error("Bad magic number in the replay file");

  char ver_buf[2];

  if (read(replay_fd, ver_buf, sizeof(ver_buf)) != sizeof(ver_buf))
    throw std::runtime_error("Failed to read the replay file format version number");

  while (1)
  {
    Mysql_packet* pkt = new Mysql_packet(); // throws on OOM
    u_longlong key;

    if (pkt->replay_read(replay_fd, &key))
    {
      delete pkt;
      return; // EOF or truncated file
    }

    Mysql_stream* s = find_or_make_stream(key, pkt);

    if (!s)
    {
      delete pkt;
      continue;
    }

    s->append_packet(pkt);
  }
}

u_longlong Mysql_stream_manager::get_packet_ellapsed_us(Mysql_packet* p)
{
    if (!first_packet_ts_inited)
        return 0;

    long long d_us = p->ts.tv_usec - first_packet_ts.tv_usec;
    long long d_s = p->ts.tv_sec - first_packet_ts.tv_sec;
    return d_s * 1000000 + d_us;
}

void Mysql_stream_manager::register_query(Mysql_stream* s, Mysql_query_packet* query)
{
    query->mark_ref();
    // TODO: if we are doing a replay, we should fill up the slow query list based on replay, not the original
    slow_queries.insert(query);

    if (slow_queries.size() > info->n_slow_queries)
    {
        std::multiset<Mysql_query_packet*>::iterator it = --slow_queries.end();
        Mysql_query_packet* p = *it;
        slow_queries.erase(it);
        s->consider_unlink_pkt(p);
    }

    if (!info->do_run)
    {
        char lookup_key[1024];
        size_t lookup_key_len = sizeof(lookup_key) - 1;
        get_query_key(lookup_key, &lookup_key_len, query->query(), query->query_len());
        lookup_key[lookup_key_len] = 0;
        q_stats.record_query(lookup_key, query->exec_time);
    }
}

void Mysql_stream_manager::print_slow_queries()
{
    if (info->do_explain || info->do_analyze)
    {
        if (!connect_for_explain())
        {
            fprintf(stderr, "Cannot do EXPLAIN/ANALYZE, no connectinn\n");
            // we can still print the queries
        }
    }

    for (std::multiset<Mysql_query_packet*>::iterator it = slow_queries.begin();
         it != slow_queries.end(); it++)
    {
        Mysql_query_packet* p = *it;
        p->print_query();

        if ((info->do_explain || info->do_analyze) && explain_con)
            explain_query(p, info->do_analyze);
    }
}

void Mysql_stream_manager::get_query_key(char* key_buf, size_t* key_buf_len, const char* query, size_t q_len)
{
    for (size_t i = 0; i < info->query_patterns.size(); i++)
    {
        const char* key = info->query_patterns[i]->apply(query, q_len, key_buf, key_buf_len);
        if (key)
        {
            key_buf[*key_buf_len] = 0;
            return;
        }
    }

    *key_buf_len = 0;
    *key_buf = 0;
}

void Query_stats::finalize()
{
    for (std::map<std::string, Query_pattern_stats*>::iterator it = lookup.begin();
            it != lookup.end(); it++)
    {
        it->second->finalize();
    }
 }

void Query_stats::print(FILE* csv_fp)
{
    std::cout << "Overall N: " << n_queries << " total time " << total_exec_time << std:: endl;

    if (csv_fp)
        fputs("Query Pattern ID, N, Minimum execution time, Maximum Execution Time, Average Execution Time,"
        "Median Execution Time, 95pct Execution Time,Total Execution Time\n", csv_fp);

    for (std::map<std::string, Query_pattern_stats*>::iterator it = lookup.begin();
            it != lookup.end(); it++)
    {
        Query_pattern_stats* s = it->second;
        std::cout << "Query Pattern ID: " << it->first << " N: " << s->n_queries << " min: "
            << s->min_exec_time << "s max: " << s->max_exec_time << "s" <<
            " avg: " << s->total_exec_time / s->n_queries << "s total time " << s->total_exec_time << "s" << std::endl;

        // TODO: escape quotes
        if (csv_fp)
            fprintf(csv_fp, "\"%s\",%lu,%f,%f,%f,%f,%f,%f\n", it->first.c_str(), s->n_queries, s->min_exec_time,
                    s->max_exec_time,
                    s->total_exec_time / s->n_queries, s->get_median_exec_time(),
                    s->get_pct_exec_time(95),
                    s->total_exec_time);
    }

}

void Query_pattern_stats::record_query(double exec_time)
{
    n_queries++;
    total_exec_time += exec_time;
    if (exec_time < min_exec_time)
        min_exec_time = exec_time;
    if (exec_time > max_exec_time)
        max_exec_time = exec_time;

    exec_times.push_back(exec_time);
}

void Query_pattern_stats::finalize()
{
    std::sort(exec_times.begin(), exec_times.end());
}

Query_stats::~Query_stats()
{
    for (std::map<std::string, Query_pattern_stats*>::iterator it = lookup.begin();
         it != lookup.end(); it++)
    {
        delete it->second;
    }
}

void Query_stats::record_query(const char* lookup_key, double exec_time)
{
    std::lock_guard<std::mutex> guard(lock);
    std::map<std::string, Query_pattern_stats*>::iterator it;
    Query_pattern_stats* s;
    if ((it = lookup.find(lookup_key)) == lookup.end())
    {
        s = lookup[lookup_key] = new Query_pattern_stats();
    }
    else
    {
        s = it->second;
    }

    n_queries++;
    total_exec_time += exec_time;
    s->record_query(exec_time);
}

void Mysql_stream_manager::finish_replay()
{
    for (std::map<u_longlong, Mysql_stream*>::iterator it = lookup.begin(); it != lookup.end(); it++)
    {
        Mysql_stream* s = it->second;
        s->end_replay();
    }

}

void Mysql_stream_manager::print_query_stats()
{
    q_stats.finalize();
    q_stats.print(csv_fp);
}

#define MAX_PATTERN_LEN 8192

void parse_re_part(char* output, const char** argp, const char* arg_end)
{
    bool in_esc = false;
    char* p = output;
    const char* arg = *argp;

    for (; arg < arg_end;)
    {
        if (in_esc)
        {
            if (*arg != '/')
                *p++ = '\\';
            *p++ = *arg++;
            in_esc = 0;
            continue;
        }

        char c = *arg;

        switch (c)
        {
            case '\\':
                in_esc = 1;
                arg++;
                continue;
            case '/':
                arg++;
                goto done;
            default:
                *p++ = *arg++;
                continue;
        }
    }

done:
    *p = 0;
    *argp = arg;
    return;
}

void param_info::add_query_pattern(const char* arg)
{
    if (*arg == 's')
        arg++;
    if (*arg == '/')
        arg++;

    size_t arg_len = strlen(arg);
    if (arg_len > MAX_PATTERN_LEN)
        arg_len = MAX_PATTERN_LEN;

    const char* arg_end = arg + arg_len;
    char search[arg_len + 1];
    char replace[arg_len + 1];

    parse_re_part(search, &arg, arg_end);
    parse_re_part(replace, &arg, arg_end);

    Query_pattern* qp = new Query_pattern(search, replace);
    query_patterns.push_back(qp);
    //printf("search: %s replace: %s\n", search, replace);
}

