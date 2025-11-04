// File: sql_parser.yy

// --- CODE FOR SOURCE FILE (.cc) PREAMBLE ---
%code {
#include <iostream>
#include <string>
#include <cstring>
#include <cctype>
#include <cstdlib> // Required for std::realloc and std::free
#include <sstream> 
#include <algorithm> 
#include <cstdarg> // Required for va_list for safe custom strcat
#include <stdexcept> // For runtime_error

// --- MEMORY POOL (ARENA ALLOCATOR) IMPLEMENTATION (NOW DYNAMIC) ---
#define DEFAULT_POOL_SIZE 4096 // Initial 4KB size
#define YY_BUF_SIZE 256 // Max size for lexer token buffer

char* pool = nullptr; // Dynamically allocated memory block
size_t pool_capacity = 0; // Current capacity of the pool
size_t pool_used = 0;

// Function to allocate memory from the pool (NOW DYNAMIC)
char* pool_alloc(size_t size) {
    size_t new_pool_used = pool_used + size;
    
    // 1. Check if the current capacity is enough
    if (new_pool_used > pool_capacity) {
        size_t new_capacity = pool_capacity == 0 ? DEFAULT_POOL_SIZE : pool_capacity;
        
        // 2. Calculate the next capacity (exponential growth or minimum required)
        while (new_pool_used > new_capacity) {
            // Check for overflow before multiplying
            if (new_capacity > (size_t)-1 / 2) { 
                new_capacity = new_pool_used;
                if (new_pool_used > new_capacity) { // If requested size itself overflowed
                    std::cerr << "❌ ERROR: Requested size causes capacity overflow." << std::endl;
                    return nullptr;
                }
                break;
            }
            new_capacity *= 2; 
        }

        // 3. Reallocate the memory block
        // Use std::realloc for in-place growth if possible
        char* new_pool = (char*)std::realloc(pool, new_capacity);
        
        if (new_pool == nullptr) {
            std::cerr << "❌ ERROR: Memory reallocation failed (requested capacity: " << new_capacity << " bytes)." << std::endl;
            return nullptr;
        }
        
        pool = new_pool;
        pool_capacity = new_capacity;
    }

    // 4. Allocation is guaranteed to succeed now
    char* ptr = pool + pool_used;
    pool_used += size;
    return ptr;
}

// Function to allocate and copy a string into the pool (replaces strdup)
char* pool_strdup(const char* s) {
    if (!s) return nullptr;
    size_t len = strlen(s) + 1;
    char* ptr = pool_alloc(len);
    if (ptr) {
        memcpy(ptr, s, len);
        ptr[len-1] = '\0'; // Ensure null termination
    }
    return ptr;
}

// --- SAFE CUSTOM STRING CONCATENATION (REPLACES pool_sprintf FOR GRAMMAR RULES) ---
// This variadic function calculates the total length of 'count' strings, 
// allocates the space, and concatenates them safely within the pool.
char* pool_strcat_n(int count, ...) {
    va_list args;
    va_start(args, count);
    // Step 1: Calculate total required length
    size_t total_len = 0;
    va_list args_copy;
    va_copy(args_copy, args);
    for (int i = 0; i < count; ++i) {
        const char* s = va_arg(args_copy, const char*);
        if (s) {
            total_len += strlen(s);
        }
    }
    va_end(args_copy);

    size_t required_size = total_len + 1; // +1 for null terminator
    
    // Step 2: Allocate from pool (handles resizing)
    char* buffer = pool_alloc(required_size);
    if (!buffer) {
        va_end(args);
        return nullptr; // Pool exhausted (or reallocation failed)
    }

    // Step 3: Concatenate strings into the allocated buffer
    char* current_pos = buffer;
    for (int i = 0; i < count; ++i) {
        const char* s = va_arg(args, const char*);
        if (s) {
            size_t len = strlen(s);
            memcpy(current_pos, s, len);
            current_pos += len;
        }
    }
    va_end(args);

    *current_pos = '\0'; // Null-terminate the final string
    return buffer;
}


// Function to reset the pool
void reset_pool() {
    pool_used = 0;
}

// Function to clean up all allocated memory
void cleanup_pool() {
    if (pool) {
        std::free(pool);
        pool = nullptr;
    }
    pool_capacity = 0;
    pool_used = 0;
}
// --- END MEMORY POOL IMPLEMENTATION ---

// Global variable for line number 
int yylineno = 1;
// --- Input Buffer Management Globals ---
const char* yy_input_buffer = nullptr;
const char* yy_current_ptr = nullptr;
const char* yy_input_end = nullptr; // Pointer to 1 past the last valid character

// Function to read the next character from the in-memory buffer, replacing getchar()
int yygetc() {
    if (yy_current_ptr >= yy_input_end) { // Check against end pointer instead of '\0'
        return EOF;
    }
    int c = *yy_current_ptr;
    yy_current_ptr++;
    if (c == '\n') yylineno++;
    return c;
}

// Function to push a character back into the buffer, replacing ungetc()
void yyungetc(int c) {
    if (yy_current_ptr > yy_input_buffer) {
        yy_current_ptr--;
        if (*yy_current_ptr == '\n') yylineno--;
    }
}
}

// --- CODE FOR HEADER FILE (.h) ---
%code requires {
#include <iostream>
#include <string>
#include <cstddef> 
#include <algorithm> 
#include <sstream> 
#include <cstring> // For strdup replacement

// --- Abstract Base Class ---
class SQL_Parser {
public:
    virtual void handle_table(const char* query_type, const char* table_name) = 0;
    virtual ~SQL_Parser() {}
};

// Forward declaration of the concrete parser 
class Demo_Parser;
// Bison will generate the correct yylex prototype: 
// int yylex (YYSTYPE *yylval, YYLTYPE *yyloc, SQL_Parser *parser)
// We declare the implementation here:
extern int yylex(void *yylval_ptr, void *yyloc_ptr, SQL_Parser* parser);
// Public function declarations
int yyparse_string(SQL_Parser* parser, const char* query, size_t query_len);
// Corrected yyerror signature for reentrant parser with location tracking.
void yyerror(void *loc, SQL_Parser* parser, const char *s);
// Function to clean up the dynamic memory pool
extern void cleanup_pool();
}

// --- Bison Directives ---

// Enable pure reentrant parser (for C++)
%define api.pure full

// Define the type of the parser instance passed to yyparse
%parse-param {SQL_Parser* parser}

// This forces the parser context (SQL_Parser* parser) to be passed 
// as the third argument in the generated call to yylex inside yyparse.
%lex-param {SQL_Parser* parser} 

// Define the types of values non-terminals and tokens can return
%union {
    char *str;
    int num;
}

// Enable location tracking.
%locations

// Token Definitions (Keywords are typically uppercase)
%token SELECT FROM WHERE JOIN ON INNER LEFT RIGHT
%token AS
%token USING 
// New tokens for DML/DDL
%token INSERT INTO VALUES UPDATE SET DELETE ALTER TABLE ADD COLUMN DROP CHANGE MODIFY SHOW
%token COMMA STAR LPAREN RPAREN
%token SEMICOLON 
// NEW TOKENS for ORDER BY, LIMIT, GROUP BY, NOT, AND, OR
%token ORDER BY LIMIT GROUP NOT AND OR 
%token DESC 
// NEW TOKENS for NULL handling (RENAMED to SQL_NULL to avoid C++ conflict)
%token IS SQL_NULL 
%token ATAT // NEW: System Variable Prefix @@

// Simple Value Tokens
%token <str> ID      
%token <str> LITERAL 
%token <str> NUMBER  

// Comparator Tokens
%token EQ LT GT LE 
GE NE NE2 // Add NE2 for <>

// Non-Terminal Symbols
%type <str> query statement select_stmt show_stmt show_content show_content_token column_list table_list table_ref join_list join_clause optional_where condition comparison_expr
// New non-terminals
%type <str> insert_stmt update_stmt delete_stmt alter_stmt alter_action value value_list set_list
// Added non-terminals for new syntax features
%type <str> column_expr optional_as_alias comma_separated_table_list optional_insert_columns column_id_list
// NEW non-terminals for JOIN specification
%type <str> join_specifier
// NEW non-terminals for function arguments
%type <str> func_arg
// NEW non-terminals for SELECT extensions
%type <str> optional_group_by optional_order_by optional_limit column_id_with_direction_list column_id_with_direction
%type <str> optional_from_clause // NEW: To make FROM optional for variable/literal selects
%type <str> optional_join_type 

// --- Operator Precedence ---
%left OR
%left AND
%left EQ NE NE2 LT GT LE GE
%left 
COMMA
%nonassoc LOW_PREC 

%%

// --- Grammar Rules ---

// 1. Main entry point (Top level rule to handle optional semicolon)
query:
    statement
    { $$ = $1;
} 
|   statement SEMICOLON
    { 
        $$ = $1;
} 
;

// 1a. SQL Statement body
statement:
    select_stmt
|   insert_stmt
|   update_stmt
|   delete_stmt
|   alter_stmt
|
show_stmt
    { $$ = $1; }
;

show_stmt:
    SHOW show_content
    {
        const char *s = "SHOW ";
$$ = pool_strcat_n(2, s, $2);
        if ($$ == nullptr) YYABORT;
    }
;

// FIX: Relaxed show_content_token to accept any keyword/ID
show_content_token:
    ID {$$ = $1;} 
|   SELECT {$$ = pool_strdup("SELECT");}
|   FROM {$$ = pool_strdup("FROM");}
|   WHERE {$$ = pool_strdup("WHERE");}
|   JOIN {$$ = pool_strdup("JOIN");}
|   ON {$$ = pool_strdup("ON");}
|   INNER {$$ = pool_strdup("INNER");}
|   LEFT {$$ = pool_strdup("LEFT");}
|   RIGHT {$$ = pool_strdup("RIGHT");}
|   AS {$$ = pool_strdup("AS");}
|   USING {$$ = pool_strdup("USING");}
|   ORDER {$$ = pool_strdup("ORDER");}
|   BY {$$ = pool_strdup("BY");}
|   LIMIT {$$ = pool_strdup("LIMIT");}
|   GROUP {$$ = pool_strdup("GROUP");}
|   NOT {$$ = pool_strdup("NOT");}
|   AND {$$ = pool_strdup("AND");}
|   OR {$$ = pool_strdup("OR");}
|   DESC {$$ = pool_strdup("DESC");}
|   INSERT {$$ = pool_strdup("INSERT");}
|   INTO {$$ = pool_strdup("INTO");}
|   VALUES {$$ = pool_strdup("VALUES");}
|   UPDATE {$$ = pool_strdup("UPDATE");}
|   SET { $$ = pool_strdup("SET");}
|   DELETE {$$ = pool_strdup("DELETE");}
|   ALTER {$$ = pool_strdup("ALTER");}
|   TABLE {$$ = pool_strdup("TABLE");}
|   ADD {$$ = pool_strdup("ADD");}
|   COLUMN {$$ = pool_strdup("COLUMN");}
|   DROP {$$ = pool_strdup("DROP");}
|   CHANGE {$$ = pool_strdup("CHANGE");}
|   MODIFY {$$ = pool_strdup("MODIFY");}
|   SHOW {$$ = pool_strdup("SHOW");}
|   IS {$$ = pool_strdup("IS");}
|   SQL_NULL {$$ = pool_strdup("NULL");} // Use SQL_NULL token, output string "NULL"
|   LITERAL {$$ = pool_strcat_n(3, "'", $1, "'");} // Keep quotes for literal to preserve type
|   NUMBER {$$ = $1;}
|   STAR {$$ = pool_strdup("*");}
;

// NEW: Helper rule for the content following SHOW (space-separated list of tokens)
show_content:
    show_content_token
    { $$ = $1;
}
|   show_content show_content_token
    {
        const char *s = " "; // Add space between tokens
        $$ = pool_strcat_n(3, $1, s, $2);
        if ($$ == nullptr) YYABORT;
    }
;
// 2. Structure of a SELECT statement
select_stmt:
    SELECT column_list optional_from_clause optional_where optional_group_by optional_order_by optional_limit
    {
        const char *s = "SELECT ";
// Use pool_strcat_n to safely concatenate strings
        $$ = pool_strcat_n(7, s, $2, $3, $4, $5, $6, $7);
if ($$ == nullptr) YYABORT;
    }
;

// NEW: Handles the optional FROM table_list clause
optional_from_clause:
    /* empty */ %prec LOW_PREC 
    { $$ = pool_strdup("");
}
|   FROM table_list
    {
        const char *s = " FROM ";
$$ = pool_strcat_n(2, s, $2); 
        if ($$ == nullptr) YYABORT;
    }
;
// 3. Handling the table list, including joins AND comma-separated tables
table_list:
    table_ref comma_separated_table_list join_list
    {
        $$ = pool_strcat_n(3, $1, $2, $3);
if ($$ == nullptr) YYABORT;
    }
;

// NEW: Handles the comma-separated list of tables (implicit join)
comma_separated_table_list:
    /* empty */
    { $$ = pool_strdup("");
}
|   comma_separated_table_list COMMA table_ref
    {
        const char *s = ", ";
$$ = pool_strcat_n(3, $1, s, $3); 
        if ($$ == nullptr) YYABORT;
    }
;
// 4. Handling the primary table reference and potential alias
table_ref:
    ID optional_as_alias 
    {
        parser->handle_table("SELECT", $1);
$$ = pool_strcat_n(2, $1, $2);
        if ($$ == nullptr) YYABORT;
    }
|
LPAREN ID RPAREN optional_as_alias 
    {
        parser->handle_table("SELECT", $2);
const char *s1 = "(", *s2 = ")";
        $$ = pool_strcat_n(4, s1, $2, s2, $4);
        if ($$ == nullptr) YYABORT;
}
|   LPAREN select_stmt RPAREN optional_as_alias 
    {
        const char *s1 = "(", *s2 = ")";
$$ = pool_strcat_n(4, s1, $2, s2, $4);
        if ($$ == nullptr) YYABORT;
    }
;
// NEW: Handles both implicit and explicit aliases
optional_as_alias:
    /* empty */
    { $$ = pool_strdup("");
}
|   AS ID 
    { 
        const char *s = " AS ";
$$ = pool_strcat_n(2, s, $2);
        if ($$ == nullptr) YYABORT;
    }
|
ID // Implicit alias
    { 
        const char *s = " ";
$$ = pool_strcat_n(2, s, $1);
        if ($$ == nullptr) YYABORT;
    }
;
// 5. Handling zero or more explicit joins
join_list:
    /* empty */
    { $$ = pool_strdup("");
}
|   join_list join_clause
    {
        const char *s = " ";
$$ = pool_strcat_n(3, $1, s, $2); 
        if ($$ == nullptr) YYABORT;
    }
;
// 7. Optional join type keywords (e.g., INNER, LEFT, RIGHT)
optional_join_type:
    /* empty */
    { $$ = pool_strdup("");
}
|   INNER
    { $$ = pool_strdup("INNER "); }
|   LEFT
    { $$ = pool_strdup("LEFT ");
}
|   RIGHT
    { $$ = pool_strdup("RIGHT "); }
;
// 6. Structure of a single join clause
join_clause:
    optional_join_type JOIN table_ref join_specifier 
    {
        const char *s1 = "JOIN ";
const char *s2 = " ";
        $$ = pool_strcat_n(4, $1, s1, $3, $4); 
        if ($$ == nullptr) YYABORT;
    }
;
// NEW: Handles the ON or USING part of a JOIN clause
join_specifier:
    ON condition
    {
        const char *s = "ON ";
$$ = pool_strcat_n(2, s, $2);
        if ($$ == nullptr) YYABORT;
    }
|
USING LPAREN column_id_list RPAREN 
    {
        const char *s1 = "USING (", *s2 = ")";
$$ = pool_strcat_n(3, s1, $3, s2);
        if ($$ == nullptr) YYABORT;
    }
;
// 8. Handling the column list (now uses column_expr)
column_list:
    column_expr
    { 
        $$ = $1;
}
|
    column_list COMMA column_expr
    {
        const char *s = ", ";
$$ = pool_strcat_n(3, $1, s, $3); 
        if ($$ == nullptr) YYABORT;
    }
;
// NEW: Helper rule for function arguments (STAR, ID, or EMPTY)
func_arg:
    /* empty */
    { $$ = pool_strdup(""); } // Allows functions with no arguments (e.g., connection_id())
|   STAR
    { $$ = pool_strdup("*");
}
|   ID
    { $$ = $1; }
;
// NEW: Supports column or function expression
column_expr:
    STAR
    { $$ = pool_strdup("*"); }
|
ID optional_as_alias // Column with optional alias, e.g., id AS c1
    { 
        $$ = pool_strcat_n(2, $1, $2);
if ($$ == nullptr) YYABORT;
    } 
|   ID LPAREN func_arg RPAREN optional_as_alias // Generalized Function call, e.g., MAX(id) as m
    {
        const char *s1 = "(", *s2 = ")";
$$ = pool_strcat_n(5, $1, s1, $3, s2, $5);
        if ($$ == nullptr) YYABORT;
    }
|
ATAT ID optional_as_alias // System Variable, e.g., @@max_allowed_packet 
    {
        const char *s = "@@";
$$ = pool_strcat_n(3, s, $2, $3);
        if ($$ == nullptr) YYABORT;
    }
|
LITERAL optional_as_alias // NEW: String literal with optional alias
    { 
        const char *s1 = "'", *s2 = "'";
$$ = pool_strcat_n(4, s1, $1, s2, $2); // Re-add quotes for semantic value
        if ($$ == nullptr) YYABORT;
}
|   NUMBER optional_as_alias // NEW: Number with optional alias
    { 
        $$ = pool_strcat_n(2, $1, $2);
if ($$ == nullptr) YYABORT;
    }
;

// 9. Handling the optional WHERE clause
optional_where:
    /* empty */
    { $$ = pool_strdup("");
}
|   WHERE condition
    {
        const char *s = "WHERE ";
$$ = pool_strcat_n(2, s, $2); 
        if ($$ == nullptr) YYABORT;
    }
;
// 10. Handling a complex condition (UPDATED for AND/OR, parentheses, and IS NULL/IS NOT NULL)
condition:
    condition OR condition
    {
        const char *s1 = "(", *s2 = ") OR (", *s3 = ")";
$$ = pool_strcat_n(5, s1, $1, s2, $3, s3);
        if ($$ == nullptr) YYABORT;
    }
|
condition AND condition
    {
        const char *s1 = "(", *s2 = ") AND (", *s3 = ")";
$$ = pool_strcat_n(5, s1, $1, s2, $3, s3);
        if ($$ == nullptr) YYABORT;
    }
|
NOT condition
    {
        const char *s1 = "NOT (", *s2 = ")";
$$ = pool_strcat_n(3, s1, $2, s2);
        if ($$ == nullptr) YYABORT;
    }
|
LPAREN condition RPAREN
    {
        const char *s1 = "(", *s2 = ")";
$$ = pool_strcat_n(3, s1, $2, s2);
        if ($$ == nullptr) YYABORT;
    }
|   comparison_expr
    { $$ = $1;
}
|   ID IS SQL_NULL // NEW: ID IS NULL
    {
        const char *s = " IS NULL";
        $$ = pool_strcat_n(2, $1, s);
        if ($$ == nullptr) YYABORT;
    }
|   ID IS NOT SQL_NULL // NEW: ID IS NOT NULL
    {
        const char *s = " IS NOT NULL";
        $$ = pool_strcat_n(2, $1, s);
        if ($$ == nullptr) YYABORT;
    }
;

// NEW: Basic comparison expression (the building block for conditions)
comparison_expr:
    ID EQ value
    {
        const char *s = " = ";
$$ = pool_strcat_n(3, $1, s, $3); 
        if ($$ == nullptr) YYABORT;
    }
|
ID NE value
    {
        const char *s = " != ";
$$ = pool_strcat_n(3, $1, s, $3); 
        if ($$ == nullptr) YYABORT;
    }
|
ID NE2 value
    {
        const char *s = " <> ";
$$ = pool_strcat_n(3, $1, s, $3); 
        if ($$ == nullptr) YYABORT;
    }
|
ID GT value
    {
        const char *s = " > ";
$$ = pool_strcat_n(3, $1, s, $3); 
        if ($$ == nullptr) YYABORT;
    }
|
ID LT value
    {
        const char *s = " < ";
$$ = pool_strcat_n(3, $1, s, $3); 
        if ($$ == nullptr) YYABORT;
    }
|
ID GE value 
    {
        const char *s = " >= ";
$$ = pool_strcat_n(3, $1, s, $3); 
        if ($$ == nullptr) YYABORT;
    }
|
ID LE value 
    {
        const char *s = " <= ";
$$ = pool_strcat_n(3, $1, s, $3); 
        if ($$ == nullptr) YYABORT;
    }
;
// NEW: 15. Optional GROUP BY clause
optional_group_by:
    /* empty */
    { $$ = pool_strdup("");
}
|   GROUP BY column_id_list
    {
        const char *s = " GROUP BY ";
$$ = pool_strcat_n(2, s, $3);
        if ($$ == nullptr) YYABORT;
    }
;
// NEW: 16. Optional ORDER BY clause
optional_order_by:
    /* empty */
    { $$ = pool_strdup("");
}
|   ORDER BY column_id_with_direction_list
    {
        const char *s = " ORDER BY ";
$$ = pool_strcat_n(2, s, $3);
        if ($$ == nullptr) YYABORT;
    }
;
// NEW: Helper rule for column list with optional ASC/DESC
column_id_with_direction_list:
    column_id_with_direction
    { $$ = $1;
}
|   column_id_with_direction_list COMMA column_id_with_direction
    {
        const char *s = ", ";
$$ = pool_strcat_n(3, $1, s, $3); 
        if ($$ == nullptr) YYABORT;
    }
;
// NEW: Helper rule for a single column with optional direction
column_id_with_direction:
    ID
    { $$ = $1;
}
|   ID DESC 
    { 
        const char *s = " DESC";
$$ = pool_strcat_n(2, $1, s);
        if ($$ == nullptr) YYABORT;
    }
;
// NEW: 17. Optional LIMIT clause
optional_limit:
    /* empty */
    { $$ = pool_strdup(""); }
|
LIMIT NUMBER
    {
        const char *s = " LIMIT ";
$$ = pool_strcat_n(2, s, $2);
        if ($$ == nullptr) YYABORT;
    }
;
// --- DML/DDL Rules ---

// 11. INSERT Statement: INSERT INTO table_name optional_column_list VALUES (value, ...)
insert_stmt:
    INSERT INTO ID optional_insert_columns VALUES LPAREN value_list RPAREN
    {
        parser->handle_table("INSERT", $3);
const char *s1 = "INSERT INTO ", *s2 = " VALUES (...)";
        $$ = pool_strcat_n(4, s1, $3, $4, s2);
if ($$ == nullptr) YYABORT;
    }
;

// NEW: Handles the optional column list in INSERT
optional_insert_columns:
    /* empty */
    { $$ = pool_strdup("");
}
|   LPAREN column_id_list RPAREN
    {
        const char *s1 = " (", *s2 = ")";
$$ = pool_strcat_n(3, s1, $2, s2);
        if ($$ == nullptr) YYABORT;
    }
;
// NEW: A comma-separated list of column IDs (used by GROUP BY and INSERT)
column_id_list:
    ID
    { $$ = $1;
}
|   column_id_list COMMA ID
    {
        const char *s = ", ";
$$ = pool_strcat_n(3, $1, s, $3); 
        if ($$ == nullptr) YYABORT;
    }
;
// 12. UPDATE Statement: UPDATE table_name SET set_list optional_where
update_stmt:
    UPDATE ID SET set_list optional_where
    {
        parser->handle_table("UPDATE", $2);
const char *s1 = "UPDATE ", *s2 = " SET (...) ", *s3 = "";
// $3 is table name
        $$ = pool_strcat_n(4, s1, $2, s2, $5);
if ($$ == nullptr) YYABORT;
    }
;

// 13. DELETE Statement: DELETE FROM table_name optional_where
delete_stmt:
    DELETE FROM ID optional_where
    {
        parser->handle_table("DELETE", $3);
const char *s1 = "DELETE FROM ", *s2 = " ";
        $$ = pool_strcat_n(3, s1, $3, $4);
if ($$ == nullptr) YYABORT;
    }
;

// 14. ALTER TABLE Statement: ALTER TABLE table_name alter_action
alter_stmt:
    ALTER TABLE ID alter_action
    {
        parser->handle_table("ALTER", $3);
const char *s1 = "ALTER TABLE ", *s2 = " (...)";
        $$ = pool_strcat_n(3, s1, $3, s2);
if ($$ == nullptr) YYABORT;
    }
;

// Helper rule for a list of values (simplified)
value_list:
    value
|
value_list COMMA value
    { /* No concatenation */ }
;
// Helper rule for a single value in INSERT/SET/WHERE
value:
    ID // ID (column name or variable)
    { $$ = $1;
}
|   LITERAL // 'String'
    { $$ = $1; }
|
NUMBER // 1 or 1.5
    { $$ = $1; }
|
LPAREN select_stmt RPAREN // NEW: Scalar Subquery (as a value)
    {
        const char *s1 = "(", *s2 = ")";
$$ = pool_strcat_n(3, s1, $2, s2);
        if ($$ == nullptr) YYABORT;
    }
;
// Helper rule for SET list (col = val)
set_list:
    ID EQ value
|   set_list COMMA ID EQ value
;
// Helper rule for ALTER actions (simplified)
alter_action:
    ADD COLUMN ID ID        
    {
        const char *s1 = "ADD COLUMN ", *s2 = " ";
$$ = pool_strcat_n(3, s1, $3, $4);
        if ($$ == nullptr) YYABORT;
    }
|
DROP COLUMN ID
    {
        const char *s = "DROP COLUMN ";
$$ = pool_strcat_n(2, s, $3);
        if ($$ == nullptr) YYABORT;
    }
|
CHANGE COLUMN ID ID ID  
    {
        const char *s1 = "CHANGE COLUMN ", *s2 = " to ", *s3 = "";
$$ = pool_strcat_n(4, s1, $3, s2, $4);
        if ($$ == nullptr) YYABORT;
    }
;
%%

// --- Auxiliary C++ Code (Implementations) ---

// Corrected yyerror implementation order: (location, parser_context, error_message)
void yyerror(void *loc, SQL_Parser* parser, const char *s) {
    std::cerr << "❌ Parse Error: " << s << " at line " << yylineno << std::endl;
}

// New function to parse a query string
int yyparse_string(SQL_Parser* parser, const char* query, size_t query_len) {
    // 1. Reset the memory pool for a fresh parse
    reset_pool();

    // 2. Set the buffer pointers and line number to use the raw buffer and length
    yy_input_buffer = query;
    yy_current_ptr = query;
    yy_input_end = query + query_len; // Set the end boundary pointer for length-based queries
    yylineno = 1;

    // 3. Call the parser (yyparse)
    int result = yyparse(parser);

    return result;
}

#ifdef TEST_SQL_PARSER

// --- Concrete Demonstration Class ---
class Demo_Parser : public SQL_Parser {
public:
    void handle_table(const char* query_type, const char* table_name) override {
        std::cout << "[HANDLER] Query Type: " << query_type 
                  << ", Table Identified: " << table_name << std::endl;
}
};

// Main function to run the parser
int main(void) {
    // Note: The memory pool is automatically reset before each parse.
const char* tests[] = {
        // Core functionality tests
        "SELECT c FROM t1 WHERE n='1';",
        "SELECT * FROM employees;",
        "UPDATE products SET price = 15.00 WHERE id = 10;",
        "ALTER TABLE logs ADD COLUMN new_col INT;",
        
        // Complex Select with System Variables
        "SELECT @@max_allowed_packet,@@system_time_zone,@@time_zone,@@auto_increment_increment;",

        // Test the previously failing query (IS NOT NULL)
        "SELECT * FROM `sys_storage_alias` WHERE `table_name` = 'sys_script_ajax' AND `element_name` IS NOT NULL /* simeverglades001 */"
    };

    std::cout << "--- Rerunning tests with DYNAMIC Memory Pool & IS NULL fix ---" << std::endl;
for (const char* query : tests) {
        std::cout << "\nParsing: " << query << std::endl;
Demo_Parser parser;
        if (yyparse_string(&parser, query, strlen(query)) == 0) {
            std::cout << "✅ Success parsing: " << query << std::endl;
} else {
            std::cout << "❌ Failed to parse: " << query << std::endl;
}
    }

    // After the last test, print memory usage and clean up
    std::cout << "\n--- Memory Pool Statistics ---" << std::endl;
    std::cout << "Final Pool Capacity: " << pool_capacity << " bytes" << std::endl;
    std::cout << "Last Parse Usage (approx): " << pool_used << " bytes" << std::endl;
    cleanup_pool(); // Clean up dynamic memory before exit
    std::cout << "Memory Pool Cleaned Up." << std::endl;
    
    return 0;
}

#endif

// ** MOCK LEXER (yylex) **
int yylex (void *yylval_ptr, void *yyloc_ptr, SQL_Parser* parser) {
    YYSTYPE* yylval = (YYSTYPE*)yylval_ptr;
// Use a static buffer with defined max size for token scanning
    static char buffer[YY_BUF_SIZE];
    int c;
char *p = buffer;

    // Helper macro for bounds check
    #define CHECK_BUF_BOUNDS(token_type) \
        if (p >= buffer + YY_BUF_SIZE - 1) { \
            std::cerr << "❌ Lexer buffer overflow! Token '" << token_type << "' too long (>" << YY_BUF_SIZE-1 << " chars)." \
            << std::endl; \
            return -1; /* Return error token */ \
        }

    // Loop until a token is found or EOF
    while ( (c = yygetc()) != EOF ) {
        // 1. Skip whitespace
        if (isspace(c)) {
            continue;
        }

        // 2. Handle Comments
        // Single-line comment: -- or #
        if (c == '-' || c == '#') {
            int next_c = yygetc();
            if (c == '-' && next_c == '-') {
                // MySQL style single-line comment: --[whitespace]...
                // We're a bit liberal here, just checking for the second dash
            } else if (c == '#') {
                // Shell/MySQL style single-line comment: #...
            } else {
                // Not a comment, push back the next char and handle the first char later
                yyungetc(next_c);
                break; // Exit comment/whitespace loop to process `c`
            }
            // If it is a single-line comment, consume until EOL or EOF
            while ((c = yygetc()) != EOF && c != '\n') {
                // consume
            }
            if (c == EOF) return 0; // End of file immediately after comment
            continue; // Go back to loop start to skip any following whitespace
        }
        
        // Multi-line comment: /* ... */
        if (c == '/') {
            int next_c = yygetc();
            if (next_c == '*') {
                // Found /*. Consume until */ or EOF
                c = yygetc(); // Get character after '*'
                while (c != EOF) {
                    if (c == '*') {
                        next_c = yygetc();
                        if (next_c == '/') {
                            break; // End of comment found: */
                        }
                        yyungetc(next_c); // If it was just *, push back the next character
                    }
                    c = yygetc(); // Get next character
                }
                if (c == EOF) {
                    std::cerr << "Unterminated multi-line comment." << std::endl;
                    return -1;
                }
                continue; // Comment consumed, go back to loop start to skip any following whitespace
            }
            yyungetc(next_c); // Not a comment, push back and handle '/' later
            break; 
        }

        // If not whitespace or comment, the character 'c' starts a token
        break; 
    }

    if (c == EOF) return 0;
// Handle single-character tokens and multi-char comparators
    if (c == ',') {
        return COMMA;
}
    if (c == '*') return STAR;
if (c == '=') return EQ;
    if (c == '<') {
        c = yygetc();
if (c == '=') return LE;
        if (c == '>') return NE2; 
        yyungetc(c);
        return LT;
}
    if (c == '>') {
        c = yygetc();
if (c == '=') return GE;
        yyungetc(c);
        return GT;
    }
    if (c == '!') { 
        c = yygetc();
if (c == '=') return NE;
        yyungetc(c);
        std::cerr << "Invalid character '!' found." << std::endl;
        return -1;
}
    if (c == '(') return LPAREN;
    if (c == ')') return RPAREN;
if (c == ';') {
        return SEMICOLON;
}
    
    // Handle @@ system variables
    if (c == '@') {
        int next_c = yygetc();
if (next_c == '@') {
            return ATAT;
}
        yyungetc(next_c); 
        // If it was a single @, it's not handled here, treat it as an error or part of ID
        std::cerr << "Invalid character '@' found." << std::endl;
        return -1;
}


    *p++ = c;
    
    // Handle backtick-quoted IDs (e.g., `id`) - ADDED BOUNDS CHECK
    if (c == '`') {
        while ( (c = yygetc()) != EOF && c != '\n' && c != '`' ) {
            CHECK_BUF_BOUNDS("backtick-quoted ID");
*p++ = c;
        }
        if (c == '`') {
            *p = '\0';
// Use pool_strdup
            yylval->str = pool_strdup(buffer + 1);
return ID;
        } else {
            std::cerr << "Unterminated backtick-quoted identifier."
<< std::endl;
            return -1;
        }
    }

    // Handle string literals (e.g., 'text') - ADDED BOUNDS CHECK
    if (c == '\'') {
        while ( (c = yygetc()) != EOF && c != '\n' && c != '\'' ) {
            CHECK_BUF_BOUNDS("literal");
*p++ = c;
        }
        if (c == '\'') {
            *p = '\0';
// Use pool_strdup
            yylval->str = pool_strdup(buffer + 1);
return LITERAL;
        } else {
            std::cerr << "Unterminated string literal."
<< std::endl; 
            return -1;
        }
    }

    // Handle numbers (integers or floats) - ADDED BOUNDS CHECK
    if (isdigit(c)) {
        bool has_dot = false;
// The first digit is already in the buffer
while ( (c = yygetc()) != EOF ) {
            CHECK_BUF_BOUNDS("number");
if (isdigit(c)) {
                *p++ = c;
} else if (c == '.' && !has_dot) {
                *p++ = c;
has_dot = true;
            } else {
                break;
}
        }
        if (c != EOF) { // FIX: Only unget if not EOF
            yyungetc(c);
}
        *p = '\0';
// Use pool_strdup
        yylval->str = pool_strdup(buffer); 
        return NUMBER;
}
    
    // Handle slash (must be an error if not handled as a comment already)
    if (c == '/') {
        // Since multi-line comments are handled before the break, a lone '/' is invalid
        std::cerr << "Invalid character '/' found." << std::endl;
        return -1;
    }


    // Handle keywords and IDs - ADDED BOUNDS CHECK
    // The current character 'c' has already been stored in buffer[0]
    while ( (c = yygetc()) != EOF && (isalnum(c) || c == '_' || c == '.') ) {
        CHECK_BUF_BOUNDS("ID/Keyword");
*p++ = c;
    }
    // FIX: Only unget if not EOF
    if (c != EOF) {
        yyungetc(c);
}
    *p = '\0';

    // Check for keywords (case-insensitive conversion to uppercase)
    // NOTE: This uses a local std::string conversion, which is fine for keywords.
std::string upper_buffer = buffer;
    
    for (char &c : upper_buffer) {
        c = static_cast<char>(toupper(c));
}

    // ... (Keyword checks) ...
    if (upper_buffer == "SELECT") return SELECT;
if (upper_buffer == "FROM") return FROM; 
    if (upper_buffer == "WHERE") return WHERE;
    if (upper_buffer == "JOIN") return JOIN;
if (upper_buffer == "ON") return ON;
    if (upper_buffer == "INNER") return INNER;
    if (upper_buffer == "LEFT") return LEFT;
if (upper_buffer == "RIGHT") return RIGHT; 
    if (upper_buffer == "AS") return AS; 
    if (upper_buffer == "USING") return USING;
if (upper_buffer == "ORDER") return ORDER;
    if (upper_buffer == "BY") return BY;
    if (upper_buffer == "LIMIT") return LIMIT;
if (upper_buffer == "GROUP") return GROUP;
    if (upper_buffer == "NOT") return NOT;
    if (upper_buffer == "DESC") return DESC;
if (upper_buffer == "AND") return AND;
    if (upper_buffer == "OR") return OR;
    if (upper_buffer == "INSERT") return INSERT;
if (upper_buffer == "INTO") return INTO;
    if (upper_buffer == "VALUES") return VALUES;
    if (upper_buffer == "UPDATE") return UPDATE;
if (upper_buffer == "SET") return SET;
    if (upper_buffer == "DELETE") return DELETE;
    if (upper_buffer == "ALTER") return ALTER;
if (upper_buffer == "TABLE") return TABLE;
    if (upper_buffer == "ADD") return ADD;
    if (upper_buffer == "COLUMN") return COLUMN;
if (upper_buffer == "DROP") return DROP;
    if (upper_buffer == "CHANGE") return CHANGE;
    if (upper_buffer == "MODIFY") return MODIFY;
if (upper_buffer == "SHOW") return SHOW;
// NEW KEYWORD CHECKS
if (upper_buffer == "IS") return IS;
if (upper_buffer == "NULL") return SQL_NULL; // Renamed token
    
    // Default: Must be an ID (column or table name)
    // Use pool_strdup
    yylval->str = pool_strdup(buffer);
return ID;
}
