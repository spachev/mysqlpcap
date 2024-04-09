#include <string.h>
#include <netinet/in.h>

#include "common.h"
#include "mysql_stream_manager.h"

/* IP header */
struct sniff_ip {
        u_char  ip_vhl;                 /* version << 4 | header length >> 2 */
        u_char  ip_tos;                 /* type of service */
        u_short ip_len;                 /* total length */
        u_short ip_id;                  /* identification */
        u_short ip_off;                 /* fragment offset field */
        #define IP_RF 0x8000            /* reserved fragment flag */
        #define IP_DF 0x4000            /* don't fragment flag */
        #define IP_MF 0x2000            /* more fragments flag */
        #define IP_OFFMASK 0x1fff       /* mask for fragmenting bits */
        u_char  ip_ttl;                 /* time to live */
        u_char  ip_p;                   /* protocol */
        u_short ip_sum;                 /* checksum */
        struct  in_addr ip_src,ip_dst;  /* source and dest address */
};

/* TCP header */
typedef u_int tcp_seq;

struct sniff_tcp {
        u_short th_sport;               /* source port */
        u_short th_dport;               /* destination port */
        tcp_seq th_seq;                 /* sequence number */
        tcp_seq th_ack;                 /* acknowledgement number */
        u_char  th_offx2;               /* data offset, rsvd */
#define TH_OFF(th)      (((th)->th_offx2 & 0xf0) >> 4)
        u_char  th_flags;
        #define TH_FIN  0x01
        #define TH_SYN  0x02
        #define TH_RST  0x04
        #define TH_PUSH 0x08
        #define TH_ACK  0x10
        #define TH_URG  0x20
        #define TH_ECE  0x40
        #define TH_CWR  0x80
        #define TH_FLAGS        (TH_FIN|TH_SYN|TH_RST|TH_ACK|TH_URG|TH_ECE|TH_CWR)
        u_short th_win;                 /* window */
        u_short th_sum;                 /* checksum */
        u_short th_urp;                 /* urgent pointer */
};

void Mysql_stream_manager::cleanup()
{
    for (std::map<u_longlong, Mysql_stream*>::iterator it = lookup.begin(); it != lookup.end(); it++)
    {
        delete (*it).second;
    }

    lookup.clear();
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
    const char* haystack = (char*)data + 4; // at this point could be invalid
    size_t cmp_len = len - 4;
    return len > 4 && data[3] == 0x3 && (strncasestr(haystack, "select", cmp_len) ||
        strncasestr(haystack, "update", cmp_len) ||
        strncasestr(haystack, "delete", cmp_len) ||
        strncasestr(haystack, "alter", cmp_len) ||
        strncasestr(haystack, "call", cmp_len)
    );
}

void Mysql_stream_manager::process_pkt(const struct pcap_pkthdr* header, const u_char* packet)
{
    int tcp_header_len;
    const struct sniff_ip* ip_header = get_ip_header(info, packet);
    if (header->caplen < (char*)ip_header + sizeof(*ip_header) - (char*)packet ||
        ip_header->ip_p != 6 /* tcp */)
            return;

    const struct sniff_tcp* tcp_header = get_tcp_header(info, packet, &tcp_header_len);
    const u_char* data = (const u_char*)tcp_header + tcp_header_len;
    bool in = (ntohl(ip_header->ip_dst.s_addr) == ntohl(mysql_ip));
    u_int len;

    if (header->caplen < (char*)data - (char*)packet)
        return; //skip weird packets

    len = header->caplen - ((char*)data - (char*)packet);
    if (ntohs(tcp_header->th_sport) != mysql_port && ntohs(tcp_header->th_dport) != mysql_port)
        return;

    u_longlong key = in ? get_key(ip_header->ip_src.s_addr, tcp_header->th_sport) :
        get_key(ip_header->ip_dst.s_addr, tcp_header->th_dport);

    Mysql_stream *s;
    std::map<u_longlong, Mysql_stream*>::iterator it;
    if ((it = lookup.find(key)) == lookup.end())
    {
        if (!(tcp_header->th_flags & TH_SYN) && !in && !could_be_query(data, len))
            return; // igore streams if we join in the middle of a conversation
        s = new Mysql_stream(this, ip_header->ip_src.s_addr, tcp_header->th_sport,
                             ip_header->ip_dst.s_addr, tcp_header->th_dport);
        lookup[key] = s;
    }
    else
    {
        s = it->second;
        if (tcp_header->th_flags & (TH_RST | TH_FIN))
        {
            lookup.erase(it);
            delete s;
            return;
        }
    }

    if (!len)
        return;

    DEBUG_MSG("key=%llu in=%d len=%u flags=%u", key, in, len, tcp_header->th_flags);
    s->append(header->ts, data, len, in);
}

void Mysql_stream_manager::register_query(Mysql_query_packet* query)
{
    slow_queries.insert(query);
    query->mark_ref();

    if (slow_queries.size() > info->n_slow_queries)
    {
        std::set<Mysql_query_packet*>::iterator it = --slow_queries.end();
        Mysql_query_packet* p = *it;
        slow_queries.erase(it);
        if (p->unmark_ref())
            delete p;
    }
}

void Mysql_stream_manager::print_slow_queries()
{
    for (std::set<Mysql_query_packet*>::iterator it = slow_queries.begin();
         it != slow_queries.end(); it++)
    {
        Mysql_query_packet* p = *it;
        p->print_query();
    }
}
