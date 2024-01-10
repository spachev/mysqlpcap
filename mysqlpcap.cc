#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <getopt.h>
#include <pcap.h>

#include "common.h"
#include "mysql_stream_manager.h"


static struct option long_options[] =
        {
          {"input",    required_argument, 0, 'i'},
          {"port",     required_argument, 0, 'p'},
          {"ip",       required_argument, 0, 'h'},
          {0, 0, 0, 0}
        };

static const char* fname = NULL;
static u_int mysql_port = 3306;
static struct in_addr mysql_ip;

void die(const char* msg, ...)
{
    va_list ap;
    va_start(ap, msg);
    fprintf(stderr, "Error: ");
    vfprintf(stderr, msg, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}


void parse_args(int argc, char** argv)
{
    inet_aton("127.0.0.1", &mysql_ip);

    while (1)
    {
        int option_index = 0;
        int c = getopt_long (argc, argv, "i:p:h:",
                       long_options, &option_index);

        if (c == -1)
            break;

        switch (c)
        {
            case 'i':
                fname = optarg;
                break;
            case 'p':
                mysql_port = atoi(optarg);
                break;
            case 'h':
                if (!inet_aton(optarg, &mysql_ip))
                    die("Invalid IP: %s", optarg);
                break;
        }

    }

    if (!fname)
        die("Missing file name, specify with -i argument");
}

#define PCAP_DIE(msg) die("pcap error: %s: %s", msg, error_buffer)

void process_file(const char* fname)
{
    char error_buffer[PCAP_ERRBUF_SIZE];
    struct bpf_program filter;
    char filter_expr[128];
    pcap_t *ph = pcap_open_offline(fname, error_buffer);
    unsigned long long n_packets = 0;

    if (!ph)
        PCAP_DIE("pcap_open_offline");

    snprintf(filter_expr, sizeof(filter_expr), "host %s and port %u and tcp", inet_ntoa(mysql_ip), mysql_port
             );
    if (pcap_compile(ph, &filter, filter_expr, 0, 0) == -1)
        PCAP_DIE("pcap_compile");
    if (pcap_setfilter(ph, &filter) == -1)
        PCAP_DIE("pcap_setfilter");

    Mysql_stream_manager sm(mysql_ip.s_addr, mysql_port);

    while (1)
    {
        struct pcap_pkthdr header;
        const u_char *packet =  pcap_next(ph, &header);

        if (!packet)
            break;

        try
        {
            sm.process_pkt(&header, packet);
        }
        catch (std::exception e)
        {
            die("Exception: %s", e.what());
        }
    }

    pcap_close(ph);
}

int main(int argc, char** argv)
{
    parse_args(argc, argv);
    process_file(fname);
    return 0;
}
