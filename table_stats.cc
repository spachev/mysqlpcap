#include <iostream>
#include <vector>
#include <sstream>
#include <cctype>
#include <map>
#include <cstring>
#include <ctime>
#include "table_stats.h"
#include "sql_parser.hh"

class Table_parser: public SQL_Parser
{
protected:
    Table_stats* table_stats;
    double exec_time;
public:
    Table_parser(Table_stats* table_stats, double exec_time):table_stats(table_stats),exec_time(exec_time)
    {
    }
    
    virtual ~Table_parser()
    {
    }
    
    void handle_table(const char* query_type, const char* table_name)
    {
        table_stats->update_table(table_name, query_type, exec_time);
    }
    
};

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

void Table_query_entry::print(FILE* fp)
{
    fprintf(fp, ",%lu,%.5f,%.5f,%.5f", n, min_time, max_time, total_time / n);
}

void Table_query_info::print(FILE* fp, const char* table_name)
{
    for (auto it = entries.begin(); it != entries.end(); it++)
    {
        fprintf(fp, ",%s,%s", table_name, it->first.c_str());
        it->second.print(fp);
    }
}

void Table_stats::print(FILE* fp)
{
    if (!fp)
        return;

    std::time_t now = std::time(nullptr);
    char timestamp[20];
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

    fprintf(fp, "%s", timestamp);

    for (auto it = stats.begin(); it != stats.end(); ++it)
    {
        it->second.print(fp, it->first.c_str());
    }

    fprintf(fp, "\n");
}

static bool table_char(char c)
{
    return isalnum(c) || c == '_' || c == '$';
}

void Table_query_entry::update(double exec_time)
{
    n++;

    if (exec_time > max_time)
        max_time = exec_time;
    if (exec_time < min_time)
        min_time = exec_time;

    total_time += exec_time;
}

void Table_query_info::register_query(const char* type, double exec_time)
{
    auto it = entries.find(type);

    if (it == entries.end())
    {
        Table_query_entry& e = entries[type] = Table_query_entry();
        e.n = 1;
        e.min_time = e.max_time = e.total_time = exec_time;
        return;
    }

    it->second.update(exec_time);
}

void Table_stats::update_table(const char* table_token, const char* type, double exec_time)
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
        Table_query_info& e = stats[table_name] = Table_query_info();
        e.register_query(type, exec_time);
        return;
    }

    it->second.register_query(type, exec_time);
}

unsigned int str_sum(const char* s, size_t len)
{
    unsigned int sum = 0;
    for (size_t i = 0; i < len; i++)
        sum += s[i];
    return sum;
}

void Table_stats::update_from_query(const char* query, size_t query_len, double exec_time)
{
    if (!query_len)
        query_len = strlen(query);

#ifdef USE_SIMPLE_PARSER
    
    std::vector<std::string> tokens = tokenize_query(std::string(query, query_len));

    if (tokens.empty())
    {
        return;
    }

    const std::string& first_token = tokens[0];
    
    // Logic to determine query type and extract tables
    if (first_token == "insert" && tokens.size() > 2 && tokens[1] == "into")
    {
        update_table(tokens[2].c_str(), first_token.c_str(), exec_time);
        return;
    }
    
    if (first_token == "update" && tokens.size() > 1)
    {
        update_table(tokens[1].c_str(), first_token.c_str(), exec_time);
        return;
    }
    
    if (first_token == "delete" && tokens.size() > 2 && tokens[1] == "from")
    {
        update_table(tokens[2].c_str(), first_token.c_str(), exec_time);
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
                    
                    update_table(table_name.c_str(), first_token.c_str(), exec_time);
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
#else
    Table_parser p(this, exec_time);
    if (yyparse_string(&p, query, query_len))
        fprintf(stderr, "Error parsing: %.*s\n", (int)query_len, query);
    else
        fprintf(stderr, "Success parsing: %.*s\n", (int)query_len, query);
#endif
}

#ifdef TEST_TABLE_STATS


int main() {
    Table_stats s;
    const char* queries[] = {
            "SELECT u.name FROM users AS u, posts p WHERE u.id = p.user_id;",
            "SELECT u.name FROM users u0, posts p0 WHERE u.id = p.user_id;",
            "INSERT INTO new_users (name) VALUES ('John');",
            "UPDATE products SET price = 15.00 WHERE id = 10;",
            "DELETE FROM old_logs WHERE date < '2023-01-01';",
            "SELECT * FROM employees;",
            "SELECT count(*) FROM employees;",
            "SELECT * FROM table1 JOIN table2 ON table1.id = table2.id;",
            "SELECT * FROM (table1) JOIN table2 ON table1.id = table2.id;",
            "SELECT * FROM `table1` JOIN table2 ON table1.id = table2.id;",
            "SELECT * FROM table_self_join t1 JOIN table_self_join t2 USING (id);",
            "SELECT t.c1 FROM (SELECT id AS c1 FROM users) AS t WHERE t.c1 = 1;",
            "SELECT name FROM users WHERE id = (SELECT max(id) FROM users);",
            "SELECT id, name FROM users WHERE NOT id = 10 ORDER BY name DESC, id LIMIT 5;",
            "SELECT dept, count(*) AS num FROM employees GROUP BY dept;",
            "SELECT product_id FROM sales WHERE quantity != 0;",
            "SELECT * FROM items WHERE price <> 100.00;",
            "SELECT name FROM users WHERE id = (SELECT max(id) FROM users) GROUP BY name ORDER BY name;",
            "SELECT * FROM large_table LIMIT 100;",
            "SELECT * FROM orders WHERE (amount > 100 AND status = 'pending') OR status = 'shipped';",
            "SELECT * FROM users WHERE age <= 25 AND NOT (city = 'NY' OR city = 'LA');",
            "select c from t1 where n='1'",
            "SELECT @@max_allowed_packet,@@system_time_zone,@@time_zone,@@auto_increment_increment",
            "select 1",
            "select 1 from dual",
            "select c from dual",
            "select c from t1",
            "select c from t1 where n=1",
            "show character set",
            "show character set; /* comment */",
            "select connection_id()",
            "SELECT `storage_alias` FROM `sys_storage_table_alias` WHERE `table_name` = 'sys_script_ajax'",
            "SELECT `storage_alias` FROM `sys_storage_table_alias` WHERE `table_name` = 'sys_script_ajax' /* simeverglades001 */ ",
            "select cast(a as signed integer) from t1",
            "select convert(a, unsigned integer) from t1",
            "select IF(a like '%unsigned%', 4,4) from t1",
            "select IF(a in ('VIRTUAL', 'PERSISTENT', 'VIRTUAL GENERATED', 'STORED GENERATED') ,'YES','NO') from t1",
            "select case a when 1 then 2 when 2 then if(a>3,3,a) else 100 end from t1",
            
            "SELECT sys_dictionary_override0.`calculation`, sys_dictionary_override0.`read_only_override`, sys_metadata0.`sys_replace_on_upgrade`, sys_metadata0.`sys_updated_on`, sys_dictionary_override0.`mandatory`, sys_metadata0.`sys_class_name`, sys_dictionary_override0.`default_value_override`, sys_dictionary_override0.`display_override`, sys_metadata0.`sys_id`, sys_metadata0.`sys_updated_by`, sys_dictionary_override0.`base_table`, sys_dictionary_override0.`calculation_override`, sys_dictionary_override0.`read_only`, sys_metadata0.`sys_created_on`, sys_metadata0.`sys_name`, sys_metadata0.`sys_scope`, sys_dictionary_override0.`dependent`, sys_metadata0.`sys_created_by`, sys_dictionary_override0.`element`, sys_dictionary_override0.`mandatory_override`, sys_dictionary_override0.`attributes_override`, sys_metadata0.`sys_mod_count`, sys_dictionary_override0.`default_value`, sys_dictionary_override0.`dependent_override`, sys_metadata0.`sys_package`, sys_metadata0.`sys_update_name`, sys_dictionary_override0.`reference_qual_override`, sys_dictionary_override0.`name`, sys_dictionary_override0.`attributes`, sys_dictionary_override0.`reference_qual`, sys_metadata0.`sys_customer_update`, sys_metadata0.`sys_policy` FROM (sys_dictionary_override sys_dictionary_override0  INNER JOIN sys_metadata sys_metadata0 ON sys_dictionary_override0.`sys_id` = sys_metadata0.`sys_id` )  WHERE sys_dictionary_override0.`base_table` = 'sys_script_ajax' /* simeverglades001 */",
            "select * from information_schema.columns",
            
            "SELECT TABLE_SCHEMA TABLE_CAT, NULL TABLE_SCHEM, TABLE_NAME, COLUMN_NAME, CASE data_type WHEN 'bit' THEN -7 WHEN 'tinyblob' THEN -3 WHEN 'mediumblob' THEN -4 WHEN 'longblob' THEN -4 WHEN 'blob' THEN -4 WHEN 'tinytext' THEN 12 WHEN 'mediumtext' THEN -1 WHEN 'longtext' THEN -1 WHEN 'text' THEN -1 WHEN 'date' THEN 91 WHEN 'datetime' THEN 93 WHEN 'decimal' THEN 3 WHEN 'double' THEN 8 WHEN 'enum' THEN 12 WHEN 'float' THEN 7 WHEN 'int' THEN IF( COLUMN_TYPE like '%unsigned%', 4,4) WHEN 'bigint' THEN -5 WHEN 'mediumint' THEN 4 WHEN 'null' THEN 0 WHEN 'set' THEN 12 WHEN 'smallint' THEN IF( COLUMN_TYPE like '%unsigned%', 5,5) WHEN 'varchar' THEN 12 WHEN 'varbinary' THEN -3 WHEN 'char' THEN 1 WHEN 'binary' THEN -2 WHEN 'time' THEN 92 WHEN 'timestamp' THEN 93 WHEN 'tinyint' THEN IF(COLUMN_TYPE like 'tinyint(1)%',-7,-6) WHEN 'year' THEN 91 ELSE 1111 END DATA_TYPE, IF(COLUMN_TYPE like 'tinyint(1)%', 'BIT', UCASE(IF( COLUMN_TYPE LIKE '%(%)%', CONCAT(SUBSTRING( COLUMN_TYPE,1, LOCATE('(',COLUMN_TYPE) - 1 ), SUBSTRING(COLUMN_TYPE ,1+locate(')', COLUMN_TYPE))), COLUMN_TYPE))) TYPE_NAME, CASE DATA_TYPE WHEN 'time' THEN IF(DATETIME_PRECISION = 0, 10, CAST(11 + DATETIME_PRECISION as signed integer)) WHEN 'date' THEN 10 WHEN 'datetime' THEN IF(DATETIME_PRECISION = 0, 19, CAST(20 + DATETIME_PRECISION as signed integer)) WHEN 'timestamp' THEN IF(DATETIME_PRECISION = 0, 19, CAST(20 + DATETIME_PRECISION as signed integer)) ELSE IF(NUMERIC_PRECISION IS NULL, LEAST(CHARACTER_MAXIMUM_LENGTH,2147483647), NUMERIC_PRECISION) END COLUMN_SIZE, 65535 BUFFER_LENGTH, CONVERT (CASE DATA_TYPE WHEN 'year' THEN NUMERIC_SCALE WHEN 'tinyint' THEN 0 ELSE NUMERIC_SCALE END, UNSIGNED INTEGER) DECIMAL_DIGITS, 10 NUM_PREC_RADIX, IF(IS_NULLABLE = 'yes',1,0) NULLABLE,COLUMN_COMMENT REMARKS, COLUMN_DEFAULT COLUMN_DEF, 0 SQL_DATA_TYPE, 0 SQL_DATETIME_SUB, LEAST(CHARACTER_OCTET_LENGTH,2147483647) CHAR_OCTET_LENGTH, ORDINAL_POSITION, IS_NULLABLE, NULL SCOPE_CATALOG, NULL SCOPE_SCHEMA, NULL SCOPE_TABLE, NULL SOURCE_DATA_TYPE, IF(EXTRA = 'auto_increment','YES','NO') IS_AUTOINCREMENT, IF(EXTRA in ('VIRTUAL', 'PERSISTENT', 'VIRTUAL GENERATED', 'STORED GENERATED') ,'YES','NO') IS_GENERATEDCOLUMN FROM INFORMATION_SCHEMA.COLUMNS WHERE (TABLE_SCHEMA = 'hi02') AND (TABLE_NAME LIKE 'sys\\_script\\_ajax') AND (COLUMN_NAME LIKE '%') ORDER BY TABLE_CAT, TABLE_SCHEM, TABLE_NAME, ORDINAL_POSITION",
        };

        const size_t num_queries = sizeof(queries) / sizeof(queries[0]);

        for (size_t i = 0; i < num_queries; ++i) {
            s.update_from_query(queries[i]);
        }
    s.print(stdout);
    return 0;
}

#endif
