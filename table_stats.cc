#include <iostream>
#include <vector>
#include <sstream>
#include <cctype>
#include <map>
#include <cstring>
#include <ctime>
#include "table_stats.h"

// Function to normalize and split the query into tokens
static std::vector<std::string> tokenize_query(const std::string& query)
{
    std::vector<std::string> tokens;
    std::string word;
    std::stringstream ss(query);
    
    while (ss >> word)
    {
        std::string lower_word;
        for (char c : word)
        {
            lower_word += tolower(static_cast<unsigned char>(c));
        }
        
        if (!lower_word.empty() && lower_word.back() == ';')
        {
            lower_word.pop_back();
        }
        tokens.push_back(lower_word);
    }
    return tokens;
}

void Table_stats::print(FILE* fp)
{
    if (!fp)
        return;

    std::time_t now = std::time(nullptr);
    char timestamp[20];
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

    // Print the timestamp at the beginning of the line.
    fprintf(fp, "%s ", timestamp);
    auto it = stats.begin();

    if (it != stats.end())
    {
        fprintf(fp, "%s,%zu", it->first.c_str(), it->second);
        ++it;
    }

    for (; it != stats.end(); ++it)
    {
        fprintf(fp, ",%s,%zu", it->first.c_str(), it->second);
    }

    fprintf(fp, "\n");
}

static bool table_char(char c)
{
    return isalnum(c) || c == '_' || c == '$';
}

void Table_stats::update_table(const char* table_token)
{
    std::string table_name;
    const char* p = table_token;

    for (; *p; p++)
        if (isalpha(*p))
            break;

    for (; *p; p++)
    {
        if (!table_char(*p))
            break;

        table_name += *p;
    }

    auto it = stats.find(table_name);
    if (it == stats.end())
    {
        stats[table_name] = 1;
        return;
    }

    it->second++;
}


// TODO: directly parse the query string without making a copy, which could be expensive for a long query

void Table_stats::update_from_query(const char* query, size_t query_len)
{
    if (!query_len)
        query_len = strlen(query);

    std::vector<std::string> tokens = tokenize_query(std::string(query, query_len));

    if (tokens.empty())
    {
        return;
    }

    const std::string& first_token = tokens[0];
    
    // Logic to determine query type and extract tables
    if (first_token == "insert" && tokens.size() > 2 && tokens[1] == "into")
    {
        update_table(tokens[2].c_str());
        return;
    }
    
    if (first_token == "update" && tokens.size() > 1)
    {
        update_table(tokens[1].c_str());
        return;
    }
    
    if (first_token == "delete" && tokens.size() > 2 && tokens[1] == "from")
    {
        update_table(tokens[2].c_str());
        return;
    }

    if (first_token == "select")
    {
        for (size_t i = 0; i < tokens.size(); ++i)
        {
            const std::string& token = tokens[i];
            
            if (token == "from" || token == "join")
            {
                size_t j = i + 1;
                while (j < tokens.size())
                {
                    std::string table_name = tokens[j];
                    
                    if (table_name == "where" || table_name == "group" || table_name == "order" || table_name == "on" ||
                        table_name == "inner" || table_name == "left" || table_name == "natural" || table_name == "right")
                    {
                        break;
                    }
                    
                    update_table(table_name.c_str());
                    j++;
                    
                    if (j < tokens.size() && tokens[j] == "as")
                    {
                        j += 2;
                    }
                    
                    if (j < tokens.size() && (tokens[j] == "join" || tokens[j] == "on" || tokens[j] == "where" || tokens[j] == "group" || tokens[j] == "order"))
                    {
                        j = tokens.size();
                    }
                }
            }
        }
    }
}

#ifdef TEST_TABLE_STATS


int main() {
    Table_stats s;
    s.update_from_query("SELECT u.name FROM users AS u, posts p WHERE u.id = p.user_id;");
    s.update_from_query("INSERT INTO new_users (name) VALUES ('John');");
    s.update_from_query("UPDATE products SET price = 15.00 WHERE id = 10;");
    s.update_from_query("DELETE FROM old_logs WHERE date < '2023-01-01';");
    s.update_from_query("SELECT * FROM employees;");
    s.update_from_query("SELECT count(*) FROM employees;");
    s.update_from_query("SELECT * FROM table1 JOIN table2 ON table1.id = table2.id;");
    s.update_from_query("SELECT * FROM (table1) JOIN table2 ON table1.id = table2.id;");
    s.update_from_query("SELECT * FROM `table1` JOIN table2 ON table1.id = table2.id;");
    s.print(stdout);
    return 0;
}

#endif
