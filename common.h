#ifndef COMMON_H
#define COMMON_H

#include <exception>
#include <chrono>
#include <assert.h>
#include <mysql.h>

typedef unsigned long long u_longlong;
typedef unsigned long long ulonglong;
typedef unsigned char uchar;
typedef unsigned long uint32;

#define REPLAY_FILE_MAGIC "MCAP"
#define REPLAY_FILE_MAGIC_LEN strlen(REPLAY_FILE_MAGIC)
#define REPLAY_FILE_VER 1

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

#define sint2korr(A)	(int16) (((int16) ((uchar) (A)[0])) |\
                ((int16) ((int16) (A)[1]) << 8))
#define sint3korr(A)	((int32) ((((uchar) (A)[2]) & 128) ? \
                (((uint32) 255L << 24) | \
                (((uint32) (uchar) (A)[2]) << 16) |\
                (((uint32) (uchar) (A)[1]) << 8) | \
                ((uint32) (uchar) (A)[0])) : \
                (((uint32) (uchar) (A)[2]) << 16) |\
                (((uint32) (uchar) (A)[1]) << 8) | \
                ((uint32) (uchar) (A)[0])))
#define sint4korr(A)	(int32) (((uint32) ((uchar) (A)[0])) |\
                (((uint32) ((uchar) (A)[1]) << 8)) |\
                (((uint32) ((uchar) (A)[2]) << 16)) |\
                (((uint32) ((uchar) (A)[3]) << 24)))
#define sint8korr(A)	(longlong) uint8korr(A)
#define uint2korr(A)	(uint16) (((uint16) ((uchar) (A)[0])) |\
                ((uint16) ((uchar) (A)[1]) << 8))
#define uint3korr(A)	(uint32) (((uint32) ((uchar) (A)[0])) |\
                (((uint32) ((uchar) (A)[1])) << 8) |\
                (((uint32) ((uchar) (A)[2])) << 16))
#define uint4korr(A)	(uint32) (((uint32) ((uchar) (A)[0])) |\
                (((uint32) ((uchar) (A)[1])) << 8) |\
                (((uint32) ((uchar) (A)[2])) << 16) |\
                (((uint32) ((uchar) (A)[3])) << 24))
#define uint5korr(A)	((ulonglong)(((uint32) ((uchar) (A)[0])) |\
                    (((uint32) ((uchar) (A)[1])) << 8) |\
                    (((uint32) ((uchar) (A)[2])) << 16) |\
                    (((uint32) ((uchar) (A)[3])) << 24)) |\
                    (((ulonglong) ((uchar) (A)[4])) << 32))
#define uint6korr(A)	((ulonglong)(((uint32)    ((uchar) (A)[0]))          | \
                                    (((uint32)    ((uchar) (A)[1])) << 8)   | \
                                    (((uint32)    ((uchar) (A)[2])) << 16)  | \
                                    (((uint32)    ((uchar) (A)[3])) << 24)) | \
                        (((ulonglong) ((uchar) (A)[4])) << 32) |       \
                        (((ulonglong) ((uchar) (A)[5])) << 40))
#define uint8korr(A)	((ulonglong)(((uint32) ((uchar) (A)[0])) |\
                    (((uint32) ((uchar) (A)[1])) << 8) |\
                    (((uint32) ((uchar) (A)[2])) << 16) |\
                    (((uint32) ((uchar) (A)[3])) << 24)) |\
            (((ulonglong) (((uint32) ((uchar) (A)[4])) |\
                    (((uint32) ((uchar) (A)[5])) << 8) |\
                    (((uint32) ((uchar) (A)[6])) << 16) |\
                    (((uint32) ((uchar) (A)[7])) << 24))) <<\
                    32))
#define int2store(T,A)       do { uint def_temp= (uint) (A) ;\
                                  *((uchar*) (T))=  (uchar)(def_temp); \
                                   *((uchar*) (T)+1)=(uchar)((def_temp >> 8)); \
                             } while(0)
#define int3store(T,A)       do { /*lint -save -e734 */\
                                  *((uchar*)(T))=(uchar) ((A));\
                                  *((uchar*) (T)+1)=(uchar) (((A) >> 8));\
                                  *((uchar*)(T)+2)=(uchar) (((A) >> 16)); \
                                  /*lint -restore */} while(0)
#define int4store(T,A)       do { *((char *)(T))=(char) ((A));\
                                  *(((char *)(T))+1)=(char) (((A) >> 8));\
                                  *(((char *)(T))+2)=(char) (((A) >> 16));\
                                  *(((char *)(T))+3)=(char) (((A) >> 24));\
                             } while(0)
#define int5store(T,A)       do { *((char *)(T))=     (char)((A));  \
                                  *(((char *)(T))+1)= (char)(((A) >> 8)); \
                                  *(((char *)(T))+2)= (char)(((A) >> 16)); \
                                  *(((char *)(T))+3)= (char)(((A) >> 24)); \
                                  *(((char *)(T))+4)= (char)(((A) >> 32)); \
                    } while(0)
#define int6store(T,A)       do { *((char *)(T))=     (char)((A)); \
                                *(((char *)(T))+1)= (char)(((A) >> 8)); \
                                *(((char *)(T))+2)= (char)(((A) >> 16)); \
                                *(((char *)(T))+3)= (char)(((A) >> 24)); \
                                *(((char *)(T))+4)= (char)(((A) >> 32)); \
                                *(((char *)(T))+5)= (char)(((A) >> 40)); \
                            } while(0)
#define int8store(T,A)       do { uint def_temp= (uint) (A), \
                                    def_temp2= (uint) ((A) >> 32); \
                                int4store((T),def_temp); \
                                int4store((T+4),def_temp2);\
                            } while(0)


#endif
