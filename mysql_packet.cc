#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


#include "common.h"
#include "mysql_packet.h"

void Mysql_packet::cleanup()
{
  if (!data)
    return;

  delete[] data;
  data = 0;
  perf_stats.pkt_mem_in_use -= len;
  perf_stats.pkt_freed++;
}

void Mysql_packet::init()
{
  DEBUG_MSG("packet len is %d", len);
  data = new u_char[len]; // throws on OOM
  perf_stats.pkt_mem_in_use += len;
  perf_stats.pkt_alloced++;
}

#define PACKET_HEADER_SIZE (8+1+16+4) // key + in + ts + data_len

// returns false on success
bool Mysql_packet::replay_write(int fd, u_longlong key)
{
  char buf[PACKET_HEADER_SIZE];
  char* p = buf;
  int8store(p, key);

  p += 8;
  *p++ = in ? 1 : 0;
  int8store(p, ts.tv_sec);
  p += 8;
  int8store(p, ts.tv_usec);
  p += 8;
  int4store(p, len);
  if (write(fd, buf, sizeof(buf)) != sizeof(buf))
    return true;

  if (!len)
    return false; // End of stream

  if (write(fd, data, len) != len)
    return true;

  return false;
}

bool Mysql_packet::replay_read(int fd, u_longlong* key)
{
  char buf[PACKET_HEADER_SIZE];
  char* p = buf;

  if (read(fd, buf, sizeof(buf)) != sizeof(buf))
  {
    data = 0;
    len = 0;
    return true;
  }

  *key = uint8korr(p);
  p += 8;
  in = *p++;
  ts.tv_sec = uint8korr(p);
  p += 8;
  ts.tv_usec = uint8korr(p);
  p += 8;
  len = uint4korr(p);

  if (!len)
    return false; // end of stream

  data = new u_char[len]; // throws on OOM
  perf_stats.pkt_mem_in_use += len;
  perf_stats.pkt_alloced++;

  if (read(fd, data, len) != len)
  {
    delete[] data;
    data = 0;
    len = 0;
    return true;
  }

  return false;
}

void Mysql_packet::append(const u_char* append_data, u_int* try_append_len)
{
  u_int append_len = *try_append_len;
  DEBUG_MSG("cur_len=%u append_len=%u len=%u", cur_len, append_len, len);

  if (cur_len + append_len > len)
  {
    append_len = len - cur_len;
  }

  if (!append_len)
    return;

  memcpy(data + cur_len, append_data, append_len);
  cur_len += append_len;
  *try_append_len -= append_len;
}

void Mysql_packet::print()
{
  printf("Packet: ");
  for (u_int i = 0; i < len; i++)
  {
    printf("%02X ", data[i]);
  }

  putchar('\n');
  if (data[0] == 0x3 && in) // Query
  {
    printf("Query: %.*s\n", len - 1, data + 1);
  }
}

double Mysql_packet::ts_diff(Mysql_packet* other)
{
  return (other->ts.tv_sec - ts.tv_sec) + (other->ts.tv_usec - ts.tv_usec) / 1000000.0;
}

bool Mysql_packet::is_query()
{
  return data[0] == 0x3 && in;
}

bool Mysql_packet::is_eof()
{
  return data[0] == 0xfe && !in;
}

