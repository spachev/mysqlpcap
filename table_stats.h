#ifndef TABLE_STATS_H
#define TABLE_STATS_H

#include <string>
#include <set>

struct Query_info
{
    std::string query_type;
    std::set<std::string> tables;
};

struct Table_query_entry
{
    size_t n;
    double min_time;
    double max_time;
    double total_time;

    void update(double exec_time);
    void print(FILE* fp);
};

struct Table_query_info
{
    std::map<std::string, Table_query_entry> entries;
    void register_query(const char* type, double exec_time);
    void print(FILE* fp);
};

struct Table_stats
{
    std::map<std::string, Table_query_info> stats;
    void print(FILE* fp);
    void update_table(const char* table_name, const char* type, double exec_time);
    void update_from_query(const char* query, size_t query_len=0, double exec_time=0.0);
};



#endif
