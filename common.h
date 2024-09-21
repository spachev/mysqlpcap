#ifndef COMMON_H
#define COMMON_H

#include <exception>
#include <chrono>
#include <assert.h>
#include <mysql.h>
#include <atomic>
#include <netinet/in.h>

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
    std::atomic_ullong pkt_mem_in_use;
    std::atomic_ullong pkt_alloced;
    std::atomic_ullong pkt_freed;

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

/* IP header */
struct sniff_ip {
        u_char  ip_vhl;                 /* version << 4 | header length >> 2 */
        u_char  ip_tos;                 /* type of service */
        u_short ip_len;                 /* total length */
        u_short ip_id;                  /* identification */
        u_short ip_off;                 /* fragment offset field */
        #define IP_RF 0x8000            /* reserved fragment flag */
        #define IP_DF 0x4000            /* don't fragment flag */
        #define IP_MF 0x2000            /* more fragments flag */
        #define IP_OFFMASK 0x1fff       /* mask for fragmenting bits */
        u_char  ip_ttl;                 /* time to live */
        u_char  ip_p;                   /* protocol */
        u_short ip_sum;                 /* checksum */
        struct  in_addr ip_src,ip_dst;  /* source and dest address */
};

/* TCP header */
typedef u_int tcp_seq;

struct sniff_tcp {
        u_short th_sport;               /* source port */
        u_short th_dport;               /* destination port */
        tcp_seq th_seq;                 /* sequence number */
        tcp_seq th_ack;                 /* acknowledgement number */
        u_char  th_offx2;               /* data offset, rsvd */
#define TH_OFF(th)      (((th)->th_offx2 & 0xf0) >> 4)
        u_char  th_flags;
        #define TH_FIN  0x01
        #define TH_SYN  0x02
        #define TH_RST  0x04
        #define TH_PUSH 0x08
        #define TH_ACK  0x10
        #define TH_URG  0x20
        #define TH_ECE  0x40
        #define TH_CWR  0x80
        #define TH_FLAGS        (TH_FIN|TH_SYN|TH_RST|TH_ACK|TH_URG|TH_ECE|TH_CWR)
        u_short th_win;                 /* window */
        u_short th_sum;                 /* checksum */
        u_short th_urp;                 /* urgent pointer */
};

#endif
