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
#include <iostream>
#include <iomanip>

#include "common.h"
#include "version.h"
#include "mysql_stream_manager.h"

enum {
  REPLAY_HOST=230,
  REPLAY_PORT,
  REPLAY_USER,
  REPLAY_PW,
  REPLAY_DB,
  REPLAY_SSL_CA,
  REPLAY_SSL_CERT,
  REPLAY_SSL_KEY,
  REPLAY_SPEED,
  PROGRESS,
  RECORD_FOR_REPLAY,
  ASSERT_ON_QUERY_ERROR,
  IGNORE_DUP_KEY_ERRORS,
  CSV,
  TABLE_STATS
};

const char* replay_host = 0;
const char* replay_user = 0;
const char* replay_pw = 0;
const char* replay_db = 0;
const char* replay_ssl_cert = 0;
const char* replay_ssl_ca = 0;
const char* replay_ssl_key = 0;
const char* record_for_replay_file = 0;


uint replay_port = 3306;
uint _mysql_port = 3306;
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
  {"replay-ssl-ca", required_argument, 0, REPLAY_SSL_CA},
  {"replay-ssl-cert", required_argument, 0, REPLAY_SSL_CERT},
  {"replay-ssl-key", required_argument, 0, REPLAY_SSL_KEY},
  {"replay-host", required_argument, 0, REPLAY_HOST},
  {"replay-db", required_argument, 0, REPLAY_DB},
  {"replay-speed", required_argument, 0, REPLAY_SPEED},
  {"query-pattern-regex", required_argument, 0, 'q'},
  {"progress", no_argument, 0, PROGRESS},
  {"record-for-replay", required_argument, 0, RECORD_FOR_REPLAY},
  {"assert-on-query-error", no_argument, 0, ASSERT_ON_QUERY_ERROR},
  {"ignore-dup-key-errors", no_argument, 0, IGNORE_DUP_KEY_ERRORS},
  {"csv", required_argument, 0, CSV},
  {"table-stats", required_argument, 0, TABLE_STATS},
  {"version", no_argument, 0, 'v'},
  {"help", no_argument, 0, 'H'},
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

void print_version()
{
     std::cerr << "mysqlpcap version " << MYSQLPCAP_VERSION << "\n";
}

void print_usage(const char* prog_name)
{
    std::cerr << "Usage: " << prog_name << " [options]\n\n";
    std::cerr << "Options for MySQL Packet Capture and Analysis:\n\n";

    // Descriptions corresponding to the long_options array (in order).
    // Note: The descriptions array MUST be kept synchronized with long_options.
    const std::vector<std::string> descriptions = {
        "Input pcap file.",
        "Target server port (used for filtering).",
        "Target server IP address (used for filtering).",
        "Print N slowest queries (N is an integer).",
        "Ethernet header size (default 14, useful for raw packets).",
        "Explain the top slow queries.",
        "Analyze queries and generate a performance summary.",
        "Run or Replay the captured queries against a target MySQL server",
        "[REPLAY] MySQL host to replay queries against.",
        "[REPLAY] MySQL port to replay queries against.",
        "[REPLAY] MySQL username for replay.",
        "[REPLAY] MySQL password for replay.",
        "[REPLAY] Path to SSL CA certificate.",
        "[REPLAY] Path to SSL certificate.",
        "[REPLAY] Path to SSL key file.",
        "[REPLAY] Target host for replay.",
        "[REPLAY] Target database name for replay.",
        "[REPLAY] Replay speed multiplier (e.g., 0.5 for half speed, 2.0 for double speed).",
        "Regex to group queries",
        "Display a progress indicator during processing or replay.",
        "Record the captured queries into a more compact MCAP file for future replay using less storage.",
        "[REPLAY] Exit immediately if a query fails during replay.",
        "[REPLAY] Ignore duplicate key errors during replay.",
        "Output analysis results to a CSV file at the specified path.",
        "Output table usage statistics (selects, updates, deletes) to the specified file.",
        "Print verision and exit",
        "Print this help message and exit"
    };

    int desc_index = 0;
    int max_option_width = 0; // To calculate optimal padding

    // 1. First pass to determine maximum option string width
    for (int i = 0; long_options[i].name != 0; ++i) {
        const struct option& opt = long_options[i];
        std::string option_str;
        
        // Add short option if it exists
        if (opt.val > 0 && opt.val < 256) {
            option_str += "-" + std::string(1, (char)opt.val) + ", ";
        } else {
            option_str += "    "; // Padding for options without a short form
        }

        option_str += "--" + std::string(opt.name);

        // Add argument type
        switch (opt.has_arg) {
            case required_argument: option_str += " <ARG>"; break;
            case optional_argument: option_str += " [ARG]"; break;
            default: break;
        }

        if (option_str.length() > max_option_width) {
            max_option_width = option_str.length();
        }
    }

    // Use a fixed margin for readability
    const int PADDING_MARGIN = 2; 
    max_option_width += PADDING_MARGIN;

    // 2. Second pass to print the options with padding
    for (int i = 0; long_options[i].name != 0; ++i)
    {
        const struct option& opt = long_options[i];

        // Check for missing description
        if (desc_index >= descriptions.size()) {
            std::cerr << "  (Error: Missing description for --" << opt.name << ")\n";
            break;
        }
        
        // Construct the option string (Option + Argument Type)
        std::string option_str;
        if (opt.val > 0 && opt.val < 256) {
            option_str += "-" + std::string(1, (char)opt.val) + ", ";
        } else {
            option_str += "    ";
        }
        option_str += "--" + std::string(opt.name);
        switch (opt.has_arg) {
            case required_argument: option_str += " <ARG>"; break;
            case optional_argument: option_str += " [ARG]"; break;
            default: break;
        }

        // Print the option string, followed by padding, and then the description
        std::cerr << "  " << std::left << std::setw(max_option_width) << option_str
                  << descriptions[desc_index++] << "\n";
    }

    std::cerr << "\n";
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
        _mysql_port = atoi(optarg);
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
      case REPLAY_SSL_CA:
        replay_ssl_ca = optarg;
        break;
      case REPLAY_SSL_CERT:
        replay_ssl_cert = optarg;
        break;
      case REPLAY_SSL_KEY:
        replay_ssl_key = optarg;
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
      case TABLE_STATS:
        info.table_stats_file = optarg;
        break;
      case 'v':
        print_version();
        exit(0);
      case 'H':
        print_usage(argv[0]);
        exit(0);
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
  Mysql_stream_manager sm(mysql_ip.s_addr, _mysql_port, &info);
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

  sm.print_query_stats();

  if (info.table_stats_file)
      sm.print_table_stats();
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
  Mysql_stream_manager sm(mysql_ip.s_addr, _mysql_port, &info);

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
