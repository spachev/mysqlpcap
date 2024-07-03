#ifndef COMMON_H
#define COMMON_H

#include <exception>
#include <chrono>
#include <assert.h>
#include <mysql.h>

extern const char* replay_host;
extern const char* replay_user;
extern const char* replay_pw;
extern const char* replay_db;
extern uint replay_port;
extern double replay_speed;

class Base_exception: public std::exception
{
};

class Oom_exception: public Base_exception
{
};

typedef unsigned long long u_longlong;

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
