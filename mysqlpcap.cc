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
          {"print-n-slow", required_argument, 0, 'n'},
          {"ethernet-header-size", required_argument, 0, 'e'},
          {0, 0, 0, 0}
        };

static const char* fname = NULL;
static u_int mysql_port = 3306;
static u_int ethernet_header_size = 14;
static struct in_addr mysql_ip;
static param_info info;

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
        int c = getopt_long (argc, argv, "i:p:n:h:e:",
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
            case 'e':
                info.ethernet_header_size = atoi(optarg);
                break;
            case 'n':
                info.n_slow_queries = atoi(optarg);
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
    pcap_t *ph = pcap_open_offline(fname, error_buffer);
    unsigned long long n_packets = 0;

    if (!ph)
        PCAP_DIE("pcap_open_offline");

    // no filter, does not work if the packets have vlan ID in the ethernet header
    Mysql_stream_manager sm(mysql_ip.s_addr, mysql_port, &info);

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
    sm.print_slow_queries();
}

int main(int argc, char** argv)
{
    info.n_slow_queries = 0;
    parse_args(argc, argv);
    process_file(fname);
    return 0;
}
