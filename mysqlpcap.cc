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

enum {
    REPLAY_HOST=240,
    REPLAY_PORT,
    REPLAY_USER,
    REPLAY_PW,
    REPLAY_DB,
    REPLAY_SPEED,
};

const char* replay_host = 0;
const char* replay_user = 0;
const char* replay_pw = 0;
const char* replay_db = 0;


uint replay_port = 3306;
uint mysql_port = 3306;
double replay_speed = 1.0;

static struct option long_options[] =
        {
          {"input",    required_argument, 0, 'i'},
          {"port",     required_argument, 0, 'p'},
          {"ip",       required_argument, 0, 'h'},
          {"print-n-slow", required_argument, 0, 'n'},
          {"ethernet-header-size", required_argument, 0, 'e'},
          {"explain", required_argument, 0, 'E'},
          {"analyze", required_argument, 0, 'A'},
          {"run", required_argument, 0, 'R'},
          {"replay-host", required_argument, 0, REPLAY_HOST},
          {"replay-port", required_argument, 0, REPLAY_PORT},
          {"replay-user", required_argument, 0, REPLAY_USER},
          {"replay-pw", required_argument, 0, REPLAY_PW},
          {"replay-host", required_argument, 0, REPLAY_HOST},
          {"replay-db", required_argument, 0, REPLAY_DB},
          {"replay-speed", required_argument, 0, REPLAY_SPEED},
          {"query-pattern-regex", required_argument, 0, 'q'},
          {0, 0, 0, 0}
        };

static const char* fname = NULL;
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
        int c = getopt_long (argc, argv, "i:p:n:h:e:EARq:",
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
            case 'E':
                info.do_explain = true;
                break;
            case 'A':
                info.do_analyze = true;
                break;
            case 'R':
                info.do_run = true;
                break;
            case 'n':
                info.n_slow_queries = atoi(optarg);
                break;
            case REPLAY_HOST:
                replay_host = optarg;
                break;
            case REPLAY_USER:
                replay_user = optarg;
                break;
            case REPLAY_PW:
                replay_pw = optarg;
                break;
            case REPLAY_DB:
                replay_db = optarg;
                break;
            case REPLAY_PORT:
                replay_port = atoi(optarg);
                break;
            case REPLAY_SPEED:
                replay_speed = atof(optarg);
                break;
            case 'q':
                info.add_query_pattern(optarg);
                break;
            default:
                die("Invalid option -%c", c);
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
    sm.init_replay();

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

    if (info.do_run)
        sm.finish_replay();
}

int main(int argc, char** argv)
{
    info.n_slow_queries = 0;
    try
    {
        parse_args(argc, argv);
    }
    catch (std::exception e)
    {
        die("Error parsing arguments: %s\n", e.what());
    }

    process_file(fname);
    return 0;
}
