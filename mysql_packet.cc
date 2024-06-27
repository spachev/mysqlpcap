#include <string.h>

#include "common.h"
#include "mysql_packet.h"

void Mysql_packet::cleanup()
{
  if (!data)
    return;

  delete[] data;
  data = 0;
}

void Mysql_packet::init()
{
  DEBUG_MSG("packet len is %d", len);
  data = new u_char[len]; // throws on OOM
}

void Mysql_packet::append(const u_char* append_data, u_int* try_append_len)
{
  u_int append_len = *try_append_len;
  DEBUG_MSG("cur_len=%u append_len=%u len=%u", cur_len, append_len, len);
  if (cur_len + append_len > len)
  {
    append_len = len - cur_len;
  }
  DO_ASSERT(append_len > 0);
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

std::chrono::time_point<std::chrono::high_resolution_clock> Mysql_packet::get_chrono_ts()
{
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

