#include <string.h>
#include <chrono>

#include "common.h"
#include "mysql_stream.h"
#include "mysql_stream_manager.h"

void setup_for_ssl(MYSQL* con, const char* ssl_ca, const char* ssl_cert, const char* ssl_key)
{
    if (con == NULL)
    {
        throw std::runtime_error("MYSQL connection handle is NULL.");
    }

    if (ssl_ca != NULL && mysql_options(con, MYSQL_OPT_SSL_CA, ssl_ca))
    {
        throw std::runtime_error(std::string("Failed to set SSL CA file path: ") + mysql_error(con));
    }

    if (ssl_cert != NULL && mysql_options(con, MYSQL_OPT_SSL_CERT, ssl_cert))
    {
        throw std::runtime_error(std::string("Failed to set SSL client certificate: ") + mysql_error(con));
    }

    if (ssl_key != NULL && mysql_options(con, MYSQL_OPT_SSL_KEY, ssl_key))
    {
        throw std::runtime_error(std::string("Failed to set SSL client key: ") + mysql_error(con));
    }
}

void Mysql_stream::start_replay()
{
  th = new std::thread(&Mysql_stream::run_replay, this);
}

void Mysql_stream::end_replay()
{
  if (!th)
    return;

  lock.lock();
  eof_lock.lock();
  reached_eof = 1;
  eof_cond.notify_one();
  eof_lock.unlock();
  lock.unlock();
  th->join();
  delete th;
  th = 0;
}

void Mysql_stream::register_replay_packet(Mysql_packet* pkt)
{
  if (!sm->in_replay_write)
    return;

  if (pkt->replay_write(sm->replay_fd, get_key(pkt)))
    throw std::runtime_error("Error writing to replay file");
}

u_longlong Mysql_stream::get_key(Mysql_packet* pkt)
{
  return pkt->in ? Mysql_stream_manager::get_key(dst_ip, dst_port) :
            Mysql_stream_manager::get_key(src_ip, src_port);
}

void Mysql_stream::run_replay()
{

  lock.lock();
  Mysql_packet* p = first;
  lock.unlock();

  if (!p)
  {
    db_close();
    return;
  }

  for (;;)
  {
    Mysql_packet* tmp;
    if (p->is_query())
      db_query((Mysql_query_packet*)p);

    lock.lock();

    if (p->next)
    {
      tmp = p;
      p = p->next;
      consider_unlink_pkt(tmp, true);
      lock.unlock();
      continue;
    }

    lock.unlock();

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

    tmp = p;
    p = p->next;
    eof_lock.unlock();
    lock.lock();
    consider_unlink_pkt(tmp, true);
    lock.unlock();
    eof_lock.lock();
  }
}

void Mysql_stream::unlink_pkt(Mysql_packet* pkt)
{
  
  if (pkt->prev)
    pkt->prev->next = pkt->next;

  if (pkt->next)
    pkt->next->prev = pkt->prev;

  if (pkt == first)
    first = first->next;

  if (pkt == last)
      last = last->prev;

  pkt->next = pkt->prev = 0;

  if (pkt->unmark_ref())
  {

    delete pkt;
  }
}

bool Mysql_stream::db_connect()
{
  if (!(con = mysql_init(NULL)))
  {
    fprintf(stderr, "Error initializing stream replay connection\n");
    return false;
  }

  try
  {
    setup_for_ssl(con, replay_ssl_ca, replay_ssl_cert, replay_ssl_key);
  }
  catch (std::runtime_error e)
  {
    fprintf(stderr, "Error initializing SSL: %s\n", e.what());
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
  if (con)
  {
    mysql_close(con);
    con = 0;
  }
}

#define PACKET_OVERFLOW_LEN 0xffffff

bool Mysql_stream::db_query(Mysql_query_packet* query_pkt)
{
  MYSQL_RES* res = 0;
  MYSQL_ROW row = 0;
  bool ret = false;
  const char* query = query_pkt->query();
  u_int q_len = query_pkt->query_len();
  Mysql_packet* cur_pkt = query_pkt;
  auto query_scheduled_ts = sm->get_scheduled_ts(query_pkt);
  auto start = std::chrono::high_resolution_clock::now();

  if (query_scheduled_ts != INVALID_TIME)
  {
    auto now = std::chrono::high_resolution_clock::now();

    if (now < query_scheduled_ts)
    {
      auto delay = query_scheduled_ts - now;
      std::this_thread::sleep_for(delay);
    }
  }

  while (cur_pkt->len == PACKET_OVERFLOW_LEN)
  {
    std::unique_lock<std::mutex> lk(eof_lock);
    while (!cur_pkt->next && !reached_eof)
      eof_cond.wait(lk);

    cur_pkt = cur_pkt->next;

    if (!cur_pkt)
      goto err;

    q_len += cur_pkt->len;
  }

  if (cur_pkt != query_pkt)
  {
    query = new char[q_len];
    char* p = (char*)query;
    cur_pkt = query_pkt;
    memcpy(p, query_pkt->query(), query_pkt->query_len());
    p += query_pkt->query_len();

    while (cur_pkt->len == PACKET_OVERFLOW_LEN)
    {
      memcpy(p, cur_pkt->data, cur_pkt->len);
      p += cur_pkt->len;
      cur_pkt = cur_pkt->next;
      assert(cur_pkt);
    }
  }

  cur_pkt = query_pkt;

  if (!db_ensure_connected())
    return false;

  start = std::chrono::high_resolution_clock::now();

  if (mysql_real_query(con, query, q_len))
  {
    fprintf(stderr, "Error running query: %.*s : %s\n", q_len, query, mysql_error(con));

    if (sm->info->ignore_dup_key_errors && mysql_errno(con) == ER_DUP_ENTRY)
        goto err;

    if (sm->info->assert_on_query_error)
      assert(false);
    goto err;
  }

  if ((res = mysql_use_result(con))) // otherwise the query does not have a result set, e.g update
  {
    while ((row = mysql_fetch_row(res)))
    {
    }
  }

  ret = true;
err:
  if (res)
    mysql_free_result(res);

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = end - start;
  char lookup_key[1024];
  size_t lookup_key_len = sizeof(lookup_key) - 1;
  sm->get_query_key(lookup_key, &lookup_key_len, query, q_len);
  lookup_key[lookup_key_len] = 0;
  sm->q_stats.record_query(lookup_key, elapsed.count());

  if (query != query_pkt->query())
    delete[] query;

  return ret;
}


bool Mysql_stream::append(struct timeval ts, const u_char* data, u_int len, bool in)
{
  std::lock_guard<std::mutex> guard(lock);
  bool created_new_packet = false;

  while (len)
  {

    if (!last || last->is_complete())
    {
      DEBUG_MSG("creating new packet");
      if (create_new_packet(ts, &data, &len, in))
        return false;

      created_new_packet = true;
    }

    if (len == 0)
      return true;

    u_int len_before_append = len;
    last->append(data, &len);
    DEBUG_MSG("after append complete status: %d", last->is_complete());
    if (last->is_complete())
      handle_packet_complete();
    data += (len_before_append - len);
  }

  return created_new_packet;
}

void Mysql_stream::cleanup()
{
  Mysql_packet* pkt = first;

  while (pkt)
  {
    //printf("in=%d eof=%d cmd=%d len=%d\n", pkt->in, pkt->is_eof(), pkt->data[0], pkt->len);
    Mysql_packet* tmp = pkt->next;
    unlink_pkt(pkt);
    pkt = tmp;
  }

  if (con)
  {
    mysql_close(con);
    con = 0;
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
  pkt->prev = last;
  last = pkt;
  return 0;
}

void Mysql_stream::consider_unlink_pkt(Mysql_packet* pkt, bool in_replay)
{
  if (!sm->info->do_run)
  {
    unlink_pkt(pkt);
    return;
  }
    
  if (in_replay)
  {
    if (pkt->ref_count == 1)
     unlink_pkt(pkt);
  }
}

void Mysql_stream::register_stream_end(struct timeval ts)
{
  if (sm->replay_fd == -1)
    return;

  Mysql_packet p;
  p.ts = ts;
  p.len = 0;
  p.in = true;
  if (p.replay_write(sm->replay_fd, get_key(&p)))
    throw std::runtime_error("Failed to write stream end");
}

void Mysql_stream::append_packet(Mysql_packet* pkt)
{
  std::lock_guard<std::mutex> guard(lock);
  pkt->mark_ref();

  if (!first)
  {
    last = first = pkt;
    handle_packet_complete();
    return;
  }

  DO_ASSERT(last);
  last->next = pkt;
  pkt->prev = last;
  last = pkt;
  handle_packet_complete();
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
    register_replay_packet(last);
    return;
  }


  if (last_query && last->is_eof())
  {
    assert(last->next == 0);
    assert(last_query->next);
    register_replay_packet(last);
    last_query->exec_time = last_query->ts_diff(last);
    //printf("Query: %.*s\n exec_time=%.6f s\n", last_query->query_len(), last_query->query(), last_query->exec_time);
    Mysql_packet* next_p = last_query->next;
    sm->register_query(this, last_query);
    
    consider_unlink_pkt(last_query);

    for (Mysql_packet* p = next_p; p; )
    {
      // for fragmented query packets
      if (p->in)
        register_replay_packet(p);

      Mysql_packet* tmp = p->next;
      
      if (p != last)
        unlink_pkt(p);
      else
        consider_unlink_pkt(p);
      p = tmp;
    }

    last_query = 0;
    return;
  }

  if (!last->in && !last->is_eof())
  {
    unlink_pkt(last);
  }
}

