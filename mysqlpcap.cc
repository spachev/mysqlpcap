#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <getopt.h>
#include <pcap.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <sys/types.h>
#include <fcntl.h>
#include <string.h>

#include "common.h"
#include "mysql_stream_manager.h"

enum {
  REPLAY_HOST=240,
  REPLAY_PORT,
  REPLAY_USER,
  REPLAY_PW,
  REPLAY_DB,
  REPLAY_SPEED,
  PROGRESS,
  RECORD_FOR_REPLAY,
  ASSERT_ON_QUERY_ERROR,
  IGNORE_DUP_KEY_ERRORS,
  CSV,
};

const char* replay_host = 0;
const char* replay_user = 0;
const char* replay_pw = 0;
const char* replay_db = 0;
const char* record_for_replay_file = 0;


uint replay_port = 3306;
uint mysql_port = 3306;
double replay_speed = 1.0;

Perf_stats perf_stats;

static struct option long_options[] =
{
  {"input",    required_argument, 0, 'i'},
  {"port",     required_argument, 0, 'p'},
  {"ip",       required_argument, 0, 'h'},
  {"print-n-slow", required_argument, 0, 'n'},
  {"ethernet-header-size", required_argument, 0, 'e'},
  {"explain", no_argument, 0, 'E'},
  {"analyze", no_argument, 0, 'A'},
  {"run", no_argument, 0, 'R'},
  {"replay-host", required_argument, 0, REPLAY_HOST},
  {"replay-port", required_argument, 0, REPLAY_PORT},
  {"replay-user", required_argument, 0, REPLAY_USER},
  {"replay-pw", required_argument, 0, REPLAY_PW},
  {"replay-host", required_argument, 0, REPLAY_HOST},
  {"replay-db", required_argument, 0, REPLAY_DB},
  {"replay-speed", required_argument, 0, REPLAY_SPEED},
  {"query-pattern-regex", required_argument, 0, 'q'},
  {"progress", no_argument, 0, PROGRESS},
  {"record-for-replay", required_argument, 0, RECORD_FOR_REPLAY},
  {"assert-on-query-error", no_argument, 0, ASSERT_ON_QUERY_ERROR},
  {"ignore-dup-key-errors", no_argument, 0, IGNORE_DUP_KEY_ERRORS},
  {"csv", required_argument, 0, CSV},
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
      case ASSERT_ON_QUERY_ERROR:
          info.assert_on_query_error = true;
          break;
      case IGNORE_DUP_KEY_ERRORS:
          info.ignore_dup_key_errors = true;
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
      case PROGRESS:
        info.report_progress = true;
        break;
      case RECORD_FOR_REPLAY:
        record_for_replay_file = optarg;
        break;
      case CSV:
        info.csv_file = optarg;
        break;
      default:
        die("Invalid option -%c", c);
    }

  }

  if (!fname)
    die("Missing file name, specify with -i argument");
}

void progress(const char* msg, ...)
{
  va_list ap;
  va_start(ap, msg);
  vfprintf(stderr, msg, ap);
  fputc('\n', stderr);
  fprintf(stderr, "pkt_mem_in_use %llu pkt_alloced %llu pkt_freed %llu\n",
          perf_stats.pkt_mem_in_use.load(), perf_stats.pkt_alloced.load(),
          perf_stats.pkt_freed.load());
  va_end(ap);
}

#define PCAP_DIE(msg) die("pcap error: %s: %s", msg, error_buffer)

void process_pcap_file(const char* fname)
{
  char error_buffer[PCAP_ERRBUF_SIZE];
  pcap_t *ph = pcap_open_offline(fname, error_buffer);
  int fd;
  pcap_dumper_t *pd = 0;
  unsigned long long n_packets = 0;
  uint last_pct = 0;

  if (!ph)
    PCAP_DIE("pcap_open_offline");

  if ((fd = pcap_get_selectable_fd(ph)) == -1)
    PCAP_DIE("Not able to get fd from the PCAP handle");

  // no filter, does not work if the packets have vlan ID in the ethernet header
  Mysql_stream_manager sm(mysql_ip.s_addr, mysql_port, &info);
  sm.init_replay();

  if (record_for_replay_file && sm.init_replay_file(record_for_replay_file))
    die("Could not open record for replay file");

  while (1)
  {
    struct pcap_pkthdr header;
    const u_char *packet =  pcap_next(ph, &header);

    if (!packet)
      break;

    if (info.report_progress)
    {
      off_t cur_pos = lseek(fd, 0, SEEK_CUR);
      uint pct = cur_pos * 100 / info.pcap_file_size;
      if (pct > last_pct)
      {
        progress("Completed: %u%%", pct);
        last_pct = pct;
      }
    }

    try
    {
      if (sm.process_pkt(&header, packet) && pd)
      {
        pcap_dump((unsigned char*)pd, &header, (unsigned char*)packet);
      }
    }
    catch (std::exception e)
    {
      die("Exception: %s", e.what());
    }
  }

  pcap_close(ph);

  if (pd)
    pcap_dump_close(pd);

  sm.print_slow_queries();

  if (info.do_run)
    sm.finish_replay();
}

void init_file_size(const char* fname)
{
  struct stat s;
  if (stat(fname, &s) == -1)
  {
    throw std::runtime_error("Could not stat pcap file " + std::string(fname));
  }

  info.pcap_file_size = s.st_size;
}

void process_replay_file(const char* fname)
{
  Mysql_stream_manager sm(mysql_ip.s_addr, mysql_port, &info);

  if (info.do_run)
    sm.init_replay();

  sm.process_replay_file(fname);
  sm.print_slow_queries();

  if (info.do_run)
    sm.finish_replay();
}

void process_file(const char* fname)
{
  char magic[4];
  bool is_replay = false;
  int fd = open(fname, O_RDONLY);

  if (fd < 0)
    die("Error opening file %s", fname);

  if (read(fd, magic, sizeof(magic)) != sizeof(magic))
    die("Error reading the magic number");

  if (memcmp(magic, REPLAY_FILE_MAGIC, REPLAY_FILE_MAGIC_LEN) == 0)
    is_replay = true;

  close(fd);

  if (is_replay)
  {
    process_replay_file(fname);
    return;
  }

  process_pcap_file(fname);
}

int main(int argc, char** argv)
{
  info.n_slow_queries = 0;
  try
  {
    parse_args(argc, argv);
    init_file_size(fname);
  }
  catch (std::exception e)
  {
    die("Error parsing arguments: %s\n", e.what());
  }

  process_file(fname);
  progress("Finished");
  return 0;
}
