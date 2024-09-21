#include "ip_stream.h"
#include "mysql_stream.h"
#include <string.h>

IP_fragment::IP_fragment(const char* data_arg, u_short len, u_short offset):len(len),offset(offset)
{
    data = new char[len];
    memcpy(data, data_arg, len);
}

IP_stream::~IP_stream()
{
    for (auto it = packet_map.begin(); it != packet_map.end();)
    {
        IP_fragment_list* fl = it->second;
        clear_fragment_list_low(fl);
        packet_map.erase(it++);
    }
}

void IP_stream::enqueue(const struct sniff_ip* ip_header, const char* data, u_short data_len)
{
    u_short key = ip_header->ip_id;
    std::map<u_short, IP_fragment_list*>::iterator it = packet_map.find(key);
    IP_fragment_list* fl;
    IP_fragment* new_f = new IP_fragment(data, data_len, ip_header->ip_off);

    if (it == packet_map.end())
    {
        packet_map[key] = fl = new IP_fragment_list;
        fl->first = fl->last = new_f;
        fl->first->next = NULL;
        fl->first->prev = NULL;
        return;
    }

    fl = it->second;
    IP_fragment* cur_f = fl->last;

    for (; cur_f; cur_f = cur_f->prev)
    {
        if (cur_f->offset < ip_header->ip_off)
        {
            new_f->next = cur_f->next;
            new_f->prev = cur_f;
            cur_f->next = new_f;
            break;
        }
    }

    if (!cur_f)
    {
        new_f->next = fl->first;
        new_f->prev = NULL;
        fl->first->prev = new_f;
        fl->first = new_f;
        return;
    }

    if (fl->last == cur_f)
    {
        fl->last = new_f;
    }
}

bool IP_stream::has_fragments(u_short ip_id)
{
    return packet_map.find(ip_id) != packet_map.end();
}

bool IP_stream::get_first_fragment(u_short ip_id, char** data, u_int* data_len)
{
    auto it = packet_map.find(ip_id);
    if (it == packet_map.end())
        return false;

    IP_fragment_list* fl = it->second;
    if (!fl->first)
        return false;

    IP_fragment* frag = fl->first;
    *data = frag->data;
    *data_len = frag->len;
    return true;
}

void IP_stream::append_remaining_packets(u_short ip_id, Mysql_stream* s, struct timeval ts, bool in)
{
    auto it = packet_map.find(ip_id);
    assert(it != packet_map.end());
    IP_fragment_list* fl = it->second;
    assert(fl->first);
    IP_fragment* frag = fl->first->next;

    while (frag)
    {
        s->append(ts, (u_char*)frag->data, frag->len, in);
        frag = frag->next;
    }
}

void IP_stream::clear_fragment_list_low(IP_fragment_list* fl)
{
    IP_fragment* frag = fl->first;

    while (frag)
    {
        IP_fragment* tmp = frag->next;
        delete frag;
        frag = tmp;
    }
}

void IP_stream::clear_fragment_list(u_short ip_id)
{
    auto it = packet_map.find(ip_id);
    if (it == packet_map.end())
        return;
    IP_fragment_list* fl = it->second;
    clear_fragment_list_low(fl);
    packet_map.erase(it);
    delete fl;
}
