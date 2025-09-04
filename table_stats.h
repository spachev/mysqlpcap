#ifndef TABLE_STATS_H
#define TABLE_STATS_H

#include <string>
#include <set>

struct Query_info
{
    std::string query_type;
    std::set<std::string> tables;
};

struct Table_stats
{
    std::map<std::string, size_t> stats;
    void print(FILE* fp);
    void update_table(const char* table_name);
    void update_from_query(const char* query, size_t query_len=0);
};



#endif
