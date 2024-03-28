#include <string.h>

#include "common.h"
#include "mysql_stream.h"
#include "mysql_stream_manager.h"


void Mysql_stream::append(struct timeval ts, const u_char* data, u_int len, bool in)
{
  while (len)
  {
    if (!last || last->is_complete())
    {
      DEBUG_MSG("creating new packet");
      if (create_new_packet(ts, &data, &len, in))
        return;
    }

    if (len == 0)
      return;

    u_int len_before_append = len;
    last->append(data, &len);
    DEBUG_MSG("after append complete status: %d", last->is_complete());
    if (last->is_complete())
      handle_packet_complete();
    data += (len_before_append - len);
  }
}

void Mysql_stream::cleanup()
{
  Mysql_packet* pkt = first;

  while (pkt)
  {
    Mysql_packet* tmp = pkt->next;
    if (pkt->unmark_ref())
      delete pkt;
    pkt = tmp;
  }
}

int Mysql_stream::create_new_packet(struct timeval ts, const u_char** data, u_int* len, bool in)
{
  u_int hdr_bytes_left = sizeof(pkt_hdr) - cur_pkt_hdr_len;
  u_int hdr_bytes = hdr_bytes_left <= *len ? hdr_bytes_left : *len;

  if (hdr_bytes)
  {
    memcpy(pkt_hdr, *data, hdr_bytes);
    *len -= hdr_bytes;
    *data += hdr_bytes;
    cur_pkt_hdr_len += hdr_bytes;
  }

  if (cur_pkt_hdr_len < sizeof(pkt_hdr))
    return 1; // still not enough bytes for the header


  Mysql_packet* pkt = new Mysql_packet(ts, get_cur_pkt_len(), in);
  pkt->mark_ref();
  cur_pkt_hdr_len = 0; // should be reset after the packet creation

  if (!first)
  {
    last = first = pkt;
    return 0;
  }

  DO_ASSERT(last);
  last->next = pkt;
  last = pkt;
  return 0;
}

void Mysql_stream::handle_packet_complete()
{
  //last->print();
  if (last->is_query())
  {
    last_query = (Mysql_query_packet*)last;
    return;
  }

  if (last_query && last->is_eof())
  {
    last_query->exec_time = last_query->ts_diff(last);
    //printf("Query: %.*s\n exec_time=%.6f s\n", last_query->query_len(), last_query->query(), last_query->exec_time);
    sm->register_query(last_query);
  }
}

