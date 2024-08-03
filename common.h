#ifndef COMMON_H
#define COMMON_H

#include <exception>
#include <chrono>
#include <assert.h>
#include <mysql.h>

typedef unsigned long long u_longlong;


extern const char* replay_host;
extern const char* replay_user;
extern const char* replay_pw;
extern const char* replay_db;
extern uint replay_port;
extern double replay_speed;

struct Perf_stats
{
    u_longlong pkt_mem_in_use;
    u_longlong pkt_alloced;
    u_longlong pkt_freed;

    Perf_stats():pkt_mem_in_use(0), pkt_alloced(0), pkt_freed(0) {}
};

extern Perf_stats perf_stats;

class Base_exception: public std::exception
{
};

class Oom_exception: public Base_exception
{
};

using Clock = std::chrono::high_resolution_clock;
using Time_Point = std::chrono::time_point<Clock>;
constexpr Time_Point INVALID_TIME = Time_Point::min();


#define DO_ASSERT(s) assert((s))

#ifndef DEBUG_OFF
#define DEBUG_MSG(msg,...) fprintf(stderr, msg "\n", ##__VA_ARGS__)
#else
#define DEBUG_MSG(msg,...)
#endif

#endif
