#include <string.h>

#include "common.h"
#include "mysql_stream.h"
#include "mysql_stream_manager.h"

void Mysql_stream::start_replay()
{
  th = new std::thread(&Mysql_stream::run_replay, this);
}

void Mysql_stream::end_replay()
{
  if (!th)
    return;

  eof_lock.lock();
  reached_eof = 1;
  eof_cond.notify_one();
  eof_lock.unlock();
  th->join();
  delete th;
  th = 0;
}

void Mysql_stream::run_replay()
{
  if (!db_connect())
    return;

  lock.lock();
  Mysql_packet* p = first;
  lock.unlock();

  if (!p)
    return;

  for (;;)
  {
    if (p->is_query())
      db_query((Mysql_query_packet*)p);

    lock.lock();
    if (p->next)
    {
      p = p->next;
      lock.unlock();
      continue;
    }

    std::unique_lock<std::mutex> lk(eof_lock);

    while (!p->next && !reached_eof)
    {
      eof_cond.wait(lk);
    }

    if (reached_eof)
    {
      db_close();
      return;
    }

    p = p->next;
  }
}

bool Mysql_stream::db_connect()
{
  if (!(con = mysql_init(NULL)))
  {
    fprintf(stderr, "Error initializing stream replay connection\n");
    return false;
  }

  if (!mysql_real_connect(con, replay_host, replay_user, replay_pw, replay_db,
      replay_port, NULL, 0))
  {
    fprintf(stderr, "Error connecting for replay: %s", mysql_error(con));
    mysql_close(con);
    con = 0;
    return false;
  }

  return true;
}

void Mysql_stream::db_close()
{
  mysql_close(con);
  con = 0;
}

bool Mysql_stream::db_query(Mysql_query_packet* query_pkt)
{
  // TODO: record stats
  MYSQL_RES* res = 0;
  MYSQL_ROW row = 0;
  const char* query = query_pkt->query();
  u_int q_len = query_pkt->query_len();
  bool ret = false;

  if (mysql_real_query(con, query, q_len) || !(res = mysql_use_result(con)))
  {
    fprintf(stderr, "Error running query: %*.s : %s\n", q_len, query, mysql_error(con));
    goto err;
  }

  while ((row = mysql_fetch_row(res)))
  {
  }

  ret = true;
err:
  if (res)
    mysql_free_result(res);

  return ret;
}


void Mysql_stream::append(struct timeval ts, const u_char* data, u_int len, bool in)
{
  std::lock_guard<std::mutex> guard(lock);

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
  eof_lock.lock();
  eof_cond.notify_one();
  eof_lock.unlock();
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

