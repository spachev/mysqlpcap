#include <string.h>

#include "common.h"
#include "mysql_packet.h"

void Mysql_packet::cleanup()
{
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
