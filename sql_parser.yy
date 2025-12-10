// File: sql_parser.yy

// --- CODE FOR SOURCE FILE (.cc) PREAMBLE ---
%code {
#include <iostream>
#include <string>
#include <cstring>
#include <cctype>
#include <cstdlib> // Required for std::realloc, std::free, std::malloc, std::calloc
#include <sstream> 
#include <algorithm> 
#include <cstdarg> // Required for va_list for safe custom strcat
#include <stdexcept> // For runtime_error

// --- MEMORY POOL (NON-MOVING MULTI-CHUNK ARENA ALLOCATOR) ---
#define DEFAULT_POOL_SIZE 4096 // Size for each new chunk
#define YY_BUF_SIZE 256 // Max size for lexer token buffer

// New chunk definition: Memory is allocated in chunks, never moved or realloc'd
typedef struct MemoryChunk {
    char* data;
    size_t capacity;
    size_t used;
    struct MemoryChunk* next;
} MemoryChunk;

// Global head and current chunk pointers
MemoryChunk* pool_head = nullptr;
MemoryChunk* pool_current_chunk = nullptr;

// Function to allocate a new chunk
MemoryChunk* allocate_new_chunk(size_t min_size) {
    // Ensure the new chunk is at least DEFAULT_POOL_SIZE or large enough for the request
    size_t capacity = DEFAULT_POOL_SIZE > min_size ? DEFAULT_POOL_SIZE : min_size;
    
    MemoryChunk* new_chunk = (MemoryChunk*)std::malloc(sizeof(MemoryChunk));
    if (!new_chunk) return nullptr;

    new_chunk->data = (char*)std::malloc(capacity);
    if (!new_chunk->data) {
        std::free(new_chunk);
        return nullptr;
    }

    new_chunk->capacity = capacity;
    new_chunk->used = 0;
    new_chunk->next = nullptr;
    return new_chunk;
}

// Function to allocate memory from the pool (FIXED: Never moves existing blocks)
char* pool_alloc(size_t size) {
    // 1. Check if the current chunk has space
    if (pool_current_chunk && pool_current_chunk->used + size <= pool_current_chunk->capacity) {
        char* ptr = pool_current_chunk->data + pool_current_chunk->used;
        pool_current_chunk->used += size;
        return ptr;
    }

    // 2. Allocate a new chunk (and link it)
    MemoryChunk* new_chunk = allocate_new_chunk(size);
    if (!new_chunk) {
        std::cerr << "❌ ERROR: Multi-chunk Memory pool exhausted." << std::endl;
        return nullptr;
    }

    // Link the new chunk
    if (!pool_head) {
        pool_head = new_chunk;
    } else {
        // Link to the last known chunk
        if (pool_current_chunk) {
            pool_current_chunk->next = new_chunk;
        } else {
             // This case should ideally not happen if pool_head is set
             pool_head = new_chunk; 
        }
    }
    pool_current_chunk = new_chunk; // The new chunk is now the active chunk

    // 3. Allocate from the new chunk (guaranteed to fit)
    char* ptr = pool_current_chunk->data + pool_current_chunk->used;
    pool_current_chunk->used += size;
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

// SIMPLIFIED STRING CONCATENATION (Now safe due to non-moving pool)
char* pool_strcat_n(int count, ...) {
    va_list args;
    va_start(args, count);
    size_t total_len = 0;
    
    // First pass: calculate total length (Safe, as pool pointers are stable)
    for (int i = 0; i < count; ++i) {
        const char* s = va_arg(args, const char*);
        if (s) {
            total_len += strlen(s);
        }
    }
    va_end(args); // va_list must be cleaned up

    // Allocate final space in the pool (will allocate a new chunk if necessary)
    size_t required_size = total_len + 1;
    char* final_ptr = pool_alloc(required_size);
    if (!final_ptr) return nullptr;

    // Second pass: Concatenate content
    va_start(args, count); // Reset va_list pointer for the second pass
    char* current_pos = final_ptr;
    for (int i = 0; i < count; ++i) {
        const char* s = va_arg(args, const char*);
        if (s) {
            size_t len = strlen(s);
            memcpy(current_pos, s, len);
            current_pos += len;
        }
    }
    *current_pos = '\0';
    va_end(args);

    return final_ptr;
}


// Function to reset and free all allocated chunks
void cleanup_pool() {
    MemoryChunk* current = pool_head;
    while (current) {
        MemoryChunk* next = current->next;
        if (current->data) {
            std::free(current->data);
        }
        std::free(current);
        current = next;
    }
    pool_head = nullptr;
    pool_current_chunk = nullptr;
}

// Function to reset the pool (by cleaning up everything)
void reset_pool() {
    cleanup_pool();
}
// --- END MEMORY POOL IMPLEMENTATION ---

// Global variable for line number 
int yylineno = 1;
int yycolno = 1;
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
    if (c == '\n') {
        yylineno++;
        yycolno = 1; // Reset column on newline
    } else {
        yycolno++; // Increment column for any other character
    }
    return c;
}

// Function to push a character back into the buffer, replacing ungetc()
void yyungetc(int c) {
    if (yy_current_ptr > yy_input_buffer) {
        yy_current_ptr--;
        if (*yy_current_ptr == '\n') yylineno--;
        else
            yycolno--;
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
%code {
#ifdef DEBUG_BISON
}
%debug
%define parse.error verbose
%code {
#endif
}
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
%token SELECT FROM WHERE JOIN ON INNER LEFT RIGHT UNION ALL
%token AS
%token USING 
// New tokens for DML/DDL
%token INSERT INTO VALUES UPDATE SET DELETE ALTER TABLE ADD COLUMN DROP CHANGE MODIFY SHOW
%token COMMA STAR LPAREN RPAREN // STAR is the wildcard/multiplication operator
%token SEMICOLON 
// NEW TOKENS for ORDER BY, LIMIT, GROUP BY, NOT, AND, OR
%token ORDER BY LIMIT GROUP NOT AND OR 
%token DESC 
// NEW TOKENS for NULL handling 
%token IS SQL_NULL LIKE IN
%token ATAT // NEW: System Variable Prefix @@
// NEW TOKENS for CASE and Functions
%token CASE WHEN THEN ELSE END LEAST // Added LEAST
%token CONVERT CAST UCASE LOCATE CONCAT SUBSTRING IF
// NEW TOKENS for Arithmetic
%token PLUS MINUS DIV 
// NEW TOKENS for Type Specifiers
%token SIGNED UNSIGNED INTEGER // Added SIGNED, UNSIGNED, INTEGER
%token FORCE IGNORE KEY INDEX DISTINCT EXISTS

// Simple Value Tokens
%token <str> ID      
%token <str> LITERAL 
%token <str> NUMBER  


%type <str> index_hint_clause index_list optional_index_hints

// Comparator Tokens
%token EQ LT GT LE 
GE NE NE2 DOT // Add NE2 for <>

// Non-Terminal Symbols
%type <str> query statement select_stmt show_stmt show_content show_content_token column_list table_list table_ref  join_list join_clause optional_where selectable_unit
 comparison_expr join_prefix select_query
// New non-terminals
%type <str> insert_stmt update_stmt delete_stmt union_stmt alter_stmt alter_action value value_list set_list
// Added non-terminals for new syntax features
%type <str> column_expr optional_as_alias comma_separated_table_list optional_insert_columns column_id_list
// NEW non-terminals for JOIN specification
%type <str> join_specifier
// NEW non-terminals for complex expressions
%type <str> func_arg func_arg_list expression case_stmt case_when_clauses case_when_clause optional_else type_name type_word
// NEW non-terminals for SELECT extensions
%type <str> optional_group_by optional_order_by optional_limit column_id_with_direction_list column_id_with_direction
%type <str> optional_from_clause 
// NEW: For robust Boolean precedence
%type <str> condition or_condition and_condition not_condition final_condition
%type <str> index_hint_chain


// --- Operator Precedence ---
%precedence HINT_PREC
%left OR
%left AND
%left LIKE 
%left EQ NE NE2 LT GT LE GE
%left PLUS MINUS 
%left STAR DIV // Using STAR for multiplication precedence
%left COMMA
%nonassoc LOW_PREC // Used for Unary Minus binding
%precedence LOWEST_PREC

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
   select_query
|   insert_stmt
|   update_stmt
|   delete_stmt
|   alter_stmt
|   show_stmt
    { $$ = $1; }
;

selectable_unit:
    select_stmt
    { $$ = $1;
    }
|   LPAREN select_stmt RPAREN
    {
        const char *s1 = "(", *s2 = ")";
        $$ = pool_strcat_n(3, s1, $2, s2);
        if ($$ == nullptr) YYABORT;
    }
;

select_query:
    union_stmt optional_order_by optional_limit
    {
        const char *s = " ";
        // Concatenate union result ($1), optional_order_by ($2), optional_limit ($3)
        $$ = pool_strcat_n(6, $1, s, $2, s, $3, s);
        if ($$ == nullptr) YYABORT;
    }
|
    union_stmt // Handles case with no final ORDER BY/LIMIT
    { $$ = $1; }
;

union_stmt:
    selectable_unit
    { $$ = $1; }
|
    union_stmt UNION selectable_unit
    {
        const char *s = " UNION ";
        // Recursively build the union string: (S1) UNION (S2)
        $$ = pool_strcat_n(3, $1, s, $3);
        if ($$ == nullptr) YYABORT;
    }
|
    union_stmt UNION ALL selectable_unit
    {
        const char *s = " UNION ALL ";
        // Recursively build the union all string: (S1) UNION ALL (S2)
        $$ = pool_strcat_n(3, $1, s, $4); // Note: $4 is the select_stmt
        if ($$ == nullptr) YYABORT;
    }
|
    LPAREN union_stmt RPAREN
    {
        $$ = $2;
    }
;

show_stmt:
    SHOW show_content
    {
        const char *s = "SHOW ";
        $$ = pool_strcat_n(2, s, $2);
        if ($$ == nullptr) YYABORT;
    }
;

// FIX: Relaxed show_content_token to accept any keyword/ID/Literal
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
|   LIKE {$$ = pool_strdup("LIKE");} 
|   CASE {$$ = pool_strdup("CASE");}
|   WHEN {$$ = pool_strdup("WHEN");}
|   THEN {$$ = pool_strdup("THEN");}
|   ELSE {$$ = pool_strdup("ELSE");}
|   END {$$ = pool_strdup("END");}
|   LEAST {$$ = pool_strdup("LEAST");}
|   SIGNED {$$ = pool_strdup("SIGNED");}
|   UNSIGNED {$$ = pool_strdup("UNSIGNED");}
|   INTEGER {$$ = pool_strdup("INTEGER");}
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
    /* empty */  %empty %prec LOW_PREC 
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
     %empty { $$ = pool_strdup("");
}
|   comma_separated_table_list COMMA table_ref
    {
        const char *s = ", ";
        $$ = pool_strcat_n(3, $1, s, $3); 
        if ($$ == nullptr) YYABORT;
    }
;

table_ref:
    ID optional_as_alias optional_index_hints
    {
        parser->handle_table("SELECT", $1);
        $$ = pool_strcat_n(2, $1, $2);
        if ($$ == nullptr) YYABORT;
    }
|   ID DOT ID optional_as_alias optional_index_hints
    {
        const char *s1 = ".", *s2 = "";
        // $1 = information_schema, $3 = columns, $4 = optional alias
        $$ = pool_strcat_n(4, $1, s1, $3, $4);
        if ($$ == nullptr) YYABORT;
    }    
|
    LPAREN ID RPAREN optional_as_alias optional_index_hints
    {
        parser->handle_table("SELECT", $2);
        const char *s1 = "(", *s2 = ")";
        $$ = pool_strcat_n(4, s1, $2, s2, $4);
        if ($$ == nullptr) YYABORT;
    }
|   LPAREN union_stmt RPAREN optional_as_alias // Handles Subquery (select_stmt)
    {
        const char *s1 = "(", *s2 = ")";
        $$ = pool_strcat_n(4, s1, $2, s2, $4);
        if ($$ == nullptr) YYABORT;
    }
|   LPAREN table_list RPAREN optional_as_alias // <-- NEW RULE: Handles (table JOIN table) alias
    {
        const char *s1 = "(", *s2 = ")";
        $$ = pool_strcat_n(4, s1, $2, s2, $4);
        if ($$ == nullptr) YYABORT;
    }
;

// NEW: Handles both implicit and explicit aliases
optional_as_alias:
    /* empty */
     %empty { $$ = pool_strdup("");
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
     %empty { $$ = pool_strdup("");
}
|   join_list join_clause
    {
        const char *s = " ";
        $$ = pool_strcat_n(3, $1, s, $2); 
        if ($$ == nullptr) YYABORT;
    }
;

join_prefix:
    INNER JOIN { $$ = pool_strdup("INNER JOIN "); }
|   LEFT JOIN { $$ = pool_strdup("LEFT JOIN "); } // Handles the LEFT JOIN case
|   RIGHT JOIN { $$ = pool_strdup("RIGHT JOIN "); }
|   JOIN { $$ = pool_strdup("JOIN "); } // Handles simple JOIN (usually INNER)
;

// 6. Structure of a single join clause (Modified)
join_clause:
    join_prefix table_ref join_specifier 
    {
        // $1 is the join type/keyword (e.g., "LEFT JOIN ")
        $$ = pool_strcat_n(3, $1, $2, $3); 
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

// NEW: A comprehensive expression non-terminal that can handle CASE, nested functions, and arithmetic
expression:
    ID DOT ID // NEW: Handles alias.column or table.column
        {
            const char *s = ".";
            $$ = pool_strcat_n(3, $1, s, $3);
            if ($$ == nullptr) YYABORT;
        }
    |   ID DOT ID DOT ID // Optional: Handles db.table.column
        {
            const char *s = ".";
            $$ = pool_strcat_n(5, $1, s, $3, s, $5);
            if ($$ == nullptr) YYABORT;
        }
    |
    value // Simple value (ID, LITERAL, NUMBER, Subquery, NULL)
    { 
        $$ = $1; 
    }
|   MINUS expression %prec LOW_PREC // Unary minus
    {
        const char *s = "-";
        $$ = pool_strcat_n(2, s, $2);
        if ($$ == nullptr) YYABORT;
    }
|   expression PLUS expression // Addition
    {
        const char *s = " + ";
        $$ = pool_strcat_n(3, $1, s, $3);
        if ($$ == nullptr) YYABORT;
    }
|   expression MINUS expression // Subtraction (Binary)
    {
        const char *s = " - ";
        $$ = pool_strcat_n(3, $1, s, $3);
        if ($$ == nullptr) YYABORT;
    }
|   expression STAR expression // Multiplication (Using STAR token for operator)
    {
        const char *s = " * ";
        $$ = pool_strcat_n(3, $1, s, $3);
        if ($$ == nullptr) YYABORT;
    }
|   expression DIV expression // Division
    {
        const char *s = " / ";
        $$ = pool_strcat_n(3, $1, s, $3);
        if ($$ == nullptr) YYABORT;
    }
|   DISTINCT expression
{
       const char *s = " DISTINCT ";
        $$ = pool_strcat_n(2, s, $2);
        if ($$ == nullptr) YYABORT;
}
|   CAST LPAREN expression AS type_name RPAREN 
    {
        const char *s1 = "CAST(", *s2 = " AS ", *s3 = ")";
        $$ = pool_strcat_n(5, s1, $3, s2, $5, s3);
        if ($$ == nullptr) YYABORT;
    }
|   CONVERT LPAREN expression COMMA type_name RPAREN // Updated to use type_name (e.g., CONVERT(..., UNSIGNED INTEGER))
    {
        const char *s1 = "CONVERT(", *s2 = ", ", *s3 = ")";
        $$ = pool_strcat_n(5, s1, $3, s2, $5, s3);
        if ($$ == nullptr) YYABORT;
    }
|   UCASE LPAREN expression RPAREN // UCASE(expression)
    {
        const char *s1 = "UCASE(", *s2 = ")";
        $$ = pool_strcat_n(3, s1, $3, s2);
        if ($$ == nullptr) YYABORT;
    }
|   LOCATE LPAREN expression COMMA expression RPAREN // LOCATE(substring, string)
    {
        const char *s1 = "LOCATE(", *s2 = ", ", *s3 = ")";
        $$ = pool_strcat_n(5, s1, $3, s2, $5, s3);
        if ($$ == nullptr) YYABORT;
    }
|   CONCAT LPAREN func_arg_list RPAREN // CONCAT(arg1, arg2, ...)
    {
        const char *s1 = "CONCAT(", *s2 = ")";
        $$ = pool_strcat_n(3, s1, $3, s2);
        if ($$ == nullptr) YYABORT;
    }
|   SUBSTRING LPAREN expression COMMA expression RPAREN // SUBSTRING(string, start)
    {
        const char *s1 = "SUBSTRING(", *s2 = ", ", *s3 = ")";
        $$ = pool_strcat_n(5, s1, $3, s2, $5, s3);
        if ($$ == nullptr) YYABORT;
    }
|   SUBSTRING LPAREN expression COMMA expression COMMA expression RPAREN // SUBSTRING(string, start, length)
    {
        const char *s1 = "SUBSTRING(", *s2 = ", ", *s3 = ", ", *s4 = ")";
        $$ = pool_strcat_n(7, s1, $3, s2, $5, s3, $7, s4);
        if ($$ == nullptr) YYABORT;
    }
|   LEAST LPAREN func_arg_list RPAREN // Added for LEAST function
    {
        const char *s1 = "LEAST(", *s2 = ")";
        $$ = pool_strcat_n(3, s1, $3, s2);
        if ($$ == nullptr) YYABORT;
    }
|   IF LPAREN func_arg_list RPAREN // Added for IF function
    {
        const char *s1 = "IF(", *s2 = ")";
        $$ = pool_strcat_n(3, s1, $3, s2);
        if ($$ == nullptr) YYABORT;
    }
|   ID LPAREN func_arg_list RPAREN // Generic function call (e.g., MAX(c1), CONNECTION_ID())
    {
        const char *s1 = "(", *s2 = ")";
        $$ = pool_strcat_n(4, $1, s1, $3, s2);
        if ($$ == nullptr) YYABORT;
    }
|   case_stmt // CASE statements
    { $$ = $1; }
|   LPAREN expression RPAREN // Parenthesized expression
    {
        const char *s1 = "(", *s2 = ")";
        $$ = pool_strcat_n(3, s1, $2, s2);
        if ($$ == nullptr) YYABORT;
    }
|   ATAT ID // System Variable (used as an expression)
    {
        const char *s = "@@";
        $$ = pool_strcat_n(2, s, $2);
        if ($$ == nullptr) YYABORT;
    }
|   EXISTS LPAREN union_stmt RPAREN
{
        $$ = pool_strcat_n(2, "EXISTS ", $3);
        if ($$ == nullptr) YYABORT;
 }
;

// NEW: Helper rule for type components (ID or reserved type keywords)
type_word:
    ID {$$ = $1;}
|   SIGNED {$$ = pool_strdup("SIGNED");}
|   UNSIGNED {$$ = pool_strdup("UNSIGNED");}
|   INTEGER {$$ = pool_strdup("INTEGER");}
;

// NEW: Helper rule for type names with spaces (e.g., 'signed integer')
type_name:
    type_word
    { $$ = $1; }
|   type_name type_word
    {
        const char *s = " ";
        $$ = pool_strcat_n(3, $1, s, $2);
        if ($$ == nullptr) YYABORT;
    }
;

// NEW: Minimal CASE statement structure 
case_stmt:
    CASE expression case_when_clauses optional_else END // CASE expression WHEN... (like CASE x WHEN 1...)
    {
        const char *s1 = "CASE ", *s2 = " ", *s3 = " END";
        $$ = pool_strcat_n(5, s1, $2, $3, $4, s3);
        if ($$ == nullptr) YYABORT;
    }
|   CASE case_when_clauses optional_else END // CASE WHEN condition THEN...
    {
        const char *s1 = "CASE ", *s2 = " END";
        $$ = pool_strcat_n(4, s1, $2, $3, s2);
        if ($$ == nullptr) YYABORT;
    }
;

case_when_clauses:
    case_when_clause
|   case_when_clauses case_when_clause
    {
        const char *s = " ";
        $$ = pool_strcat_n(3, $1, s, $2);
        if ($$ == nullptr) YYABORT;
    }
;

case_when_clause:
    WHEN expression THEN expression // Covers WHEN value THEN result (for CASE expression)
    {
        const char *s1 = "WHEN ", *s2 = " THEN ";
        $$ = pool_strcat_n(4, s1, $2, s2, $4);
        if ($$ == nullptr) YYABORT;
    }
|   WHEN condition THEN expression // Covers WHEN condition THEN result (for CASE WHEN)
    {
        const char *s1 = "WHEN ", *s2 = " THEN ";
        $$ = pool_strcat_n(4, s1, $2, s2, $4);
        if ($$ == nullptr) YYABORT;
    }
;

optional_else:
    /* empty */
     %empty { $$ = pool_strdup(""); }
|   ELSE expression
    {
        const char *s = " ELSE ";
        $$ = pool_strcat_n(2, s, $2);
        if ($$ == nullptr) YYABORT;
    }
;

// NEW: Comma separated list of arguments (allows for CONCAT(s1, s2, s3, ...))
func_arg_list:
    /* empty */ // Allows functions with no arguments (e.g., connection_id())
     %empty { $$ = pool_strdup(""); } 
|   func_arg
    { $$ = $1; }
|   func_arg_list COMMA func_arg
    {
        const char *s = ", ";
        $$ = pool_strcat_n(3, $1, s, $3); 
        if ($$ == nullptr) YYABORT;
    }
;

func_arg:
    STAR
    { $$ = pool_strdup("*");
}
|   expression
    { $$ = $1; }
|   condition // NEW: Allow boolean conditions (like a>3 for IF function)
    { $$ = $1; }
;

// NEW: Supports expression (simple value, function call, CASE)
column_expr:
    // FIX: Allow SELECT * [AS alias]
    STAR optional_as_alias
    { 
        const char *s = "*";
        $$ = pool_strcat_n(2, s, $2);
        if ($$ == nullptr) YYABORT;
    }
|
    expression optional_as_alias // Handles ID, LITERAL, NUMBER, Function calls, and CASE statements
    { 
        $$ = pool_strcat_n(2, $1, $2);
        if ($$ == nullptr) YYABORT;
    } 
;

// 9. Handling the optional WHERE clause
optional_where:
    /* empty */
     %empty { $$ = pool_strdup("");
}
|   WHERE condition
    {
        const char *s = "WHERE ";
        $$ = pool_strcat_n(2, s, $2); 
        if ($$ == nullptr) YYABORT;
    }
;

// 10a. Root condition rule
condition:
    or_condition
    { $$ = $1; }
|
    expression
    { $$ = $1; }
;

// 10b. OR precedence (lowest)
or_condition:
    or_condition OR and_condition
    {
        const char *s1 = "(", *s2 = ") OR (", *s3 = ")";
        $$ = pool_strcat_n(5, s1, $1, s2, $3, s3);
        if ($$ == nullptr) YYABORT;
    }
|   and_condition
    { $$ = $1; }
;

// 10c. AND precedence (higher than OR)
and_condition:
    and_condition AND not_condition
    {
        const char *s1 = "(", *s2 = ") AND (", *s3 = ")";
        $$ = pool_strcat_n(5, s1, $1, s2, $3, s3);
        if ($$ == nullptr) YYABORT;
    }
|   not_condition
    { $$ = $1; }
;

// 10d. NOT precedence (highest logical)
not_condition:
    NOT final_condition
    {
        const char *s1 = "NOT (", *s2 = ")";
        $$ = pool_strcat_n(3, s1, $2, s2);
        if ($$ == nullptr) YYABORT;
    }
|   final_condition
    { $$ = $1; }
;

// 10e. Base condition/terminal rules
final_condition:
    LPAREN condition RPAREN
    {
        const char *s1 = "(", *s2 = ")";
        $$ = pool_strcat_n(3, s1, $2, s2);
        if ($$ == nullptr) YYABORT;
    }
|   comparison_expr
    { $$ = $1; }
|   expression IS SQL_NULL
    {
        const char *s = " IS NULL";
        $$ = pool_strcat_n(2, $1, s);
        if ($$ == nullptr) YYABORT;
    }
|   expression IS NOT SQL_NULL
    {
        const char *s = " IS NOT NULL";
        $$ = pool_strcat_n(2, $1, s);
        if ($$ == nullptr) YYABORT;
    }
;

comparison_expr:
    expression EQ expression
    {
        const char *s = " = ";
        $$ = pool_strcat_n(3, $1, s, $3); 
        if ($$ == nullptr) YYABORT;
    }
|
    expression NE expression
    {
        const char *s = " != ";
        $$ = pool_strcat_n(3, $1, s, $3); 
        if ($$ == nullptr) YYABORT;
    }
|
    expression NE2 expression
    {
        const char *s = " <> ";
        $$ = pool_strcat_n(3, $1, s, $3); 
        if ($$ == nullptr) YYABORT;
    }
|
    expression GT expression
    {
        const char *s = " > ";
        $$ = pool_strcat_n(3, $1, s, $3); 
        if ($$ == nullptr) YYABORT;
    }
|
    expression LT expression
    {
        const char *s = " < ";
        $$ = pool_strcat_n(3, $1, s, $3); 
        if ($$ == nullptr) YYABORT;
    }
|
    expression GE expression 
    {
        const char *s = " >= ";
        $$ = pool_strcat_n(3, $1, s, $3); 
        if ($$ == nullptr) YYABORT;
    }
|
    expression LE expression 
    {
        const char *s = " <= ";
        $$ = pool_strcat_n(3, $1, s, $3); 
        if ($$ == nullptr) YYABORT;
    }
|
    expression LIKE expression 
    {
        const char *s = " LIKE ";
        $$ = pool_strcat_n(3, $1, s, $3); 
        if ($$ == nullptr) YYABORT;
    }
|
    expression IN LPAREN value_list RPAREN 
    {
        // This rule parses: expression IN ( value_list )
        const char *s1 = " IN (", *s2 = ")";
        $$ = pool_strcat_n(3, $1, s1, $4, s2);
        if ($$ == nullptr) YYABORT;
    }
|
    expression NOT IN LPAREN value_list RPAREN
    {
        // This rule parses: expression IN ( value_list )
        const char *s1 = " NOT IN (", *s2 = ")";
        $$ = pool_strcat_n(3, $1, s1, $5, s2);
        if ($$ == nullptr) YYABORT;
    }
|
    expression IN LPAREN select_stmt RPAREN
    {
        const char *s1 = " IN (", *s2 = ")";
        $$ = pool_strcat_n(3, $1, s1, $4, s2);
        if ($$ == nullptr) YYABORT;
    }
|
    expression NOT IN LPAREN select_stmt RPAREN
    {
        const char *s1 = " NOT IN (", *s2 = ")";
        $$ = pool_strcat_n(3, $1, s1, $5, s2);
        if ($$ == nullptr) YYABORT;
    }
|
    expression
    {
        $$ = $1;
    }
;
optional_group_by:
    /* empty */
     %empty { $$ = pool_strdup("");
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
     %empty { $$ = pool_strdup("");
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
    expression
    { $$ = $1;
}
|   expression DESC 
    { 
        const char *s = " DESC";
        $$ = pool_strcat_n(2, $1, s);
        if ($$ == nullptr) YYABORT;
    }
;
// NEW: 17. Optional LIMIT clause
optional_limit:
    /* empty */
     %empty { $$ = pool_strdup(""); }
|
    LIMIT NUMBER
    {
        const char *s = " LIMIT ";
        $$ = pool_strcat_n(2, s, $2);
        if ($$ == nullptr) YYABORT;
    }
|
    LIMIT NUMBER COMMA NUMBER
    {
        const char *s = " LIMIT ";
        $$ = pool_strcat_n(2, s, $2, ",", $4);
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
     %empty { $$ = pool_strdup("");
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
    column_expr
    { $$ = $1;
}
|   column_id_list COMMA column_expr
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
// Helper rule for a single value in INSERT/SET/WHERE - FIX: Ensures quotes are preserved/added for literals
value:
    ID // ID (column name or variable)
    { $$ = $1;
}
|   LITERAL // 'String'
    { 
        const char *s1 = "'", *s2 = "'";
        $$ = pool_strcat_n(3, s1, $1, s2); // Re-add quotes for semantic value
        if ($$ == nullptr) YYABORT;
    }
|
    NUMBER
    { $$ = $1; }
|
    MINUS NUMBER
    {
        $$ = pool_strcat_n(2, "-1", $2);
        if ($$ == nullptr) YYABORT;
    }
|
    LPAREN select_stmt RPAREN // NEW: Scalar Subquery (as a value)
    {
        const char *s1 = "(", *s2 = ")";
        $$ = pool_strcat_n(3, s1, $2, s2);
        if ($$ == nullptr) YYABORT;
    }
|   SQL_NULL // The literal NULL keyword 
    { $$ = pool_strdup("NULL"); }
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

// NEW: A list of index names, e.g., i1, i2, primary
index_list:
    ID
    { $$ = $1; }
|   index_list COMMA ID
    {
        const char *s = ", ";
        $$ = pool_strcat_n(3, $1, s, $3);
        if ($$ == nullptr) YYABORT;
    }
;


optional_index_hints:
    /* empty */ %empty %prec LOWEST_PREC
    { /* fprintf(stderr, "empty index hint match\n");*/ $$ = pool_strdup(""); if ($$ == nullptr) YYABORT; }
|   index_hint_chain
;

// Define the index_hint_clause to explicitly look for the full sequence.
index_hint_clause:
    // 1. The form causing the error: IGNORE INDEX (list)
    IGNORE INDEX LPAREN index_list RPAREN
    {
        const char *s1 = " IGNORE INDEX (", *s2 = ")";
        $$ = pool_strcat_n(3, s1, $4, s2); // $4 is the index_list
        if ($$ == nullptr) YYABORT;
    }
|
    // 2. The other 'key_or_index_type' variant: IGNORE KEY (list)
    IGNORE KEY LPAREN index_list RPAREN
    {
        const char *s1 = " IGNORE KEY (", *s2 = ")";
        $$ = pool_strcat_n(3, s1, $4, s2);
        if ($$ == nullptr) YYABORT;
    }
|
    // 3. The shorthand form: IGNORE (list)
    IGNORE LPAREN index_list RPAREN
    {
        const char *s1 = " IGNORE (", *s2 = ")";
        $$ = pool_strcat_n(3, s1, $3, s2);
        if ($$ == nullptr) YYABORT;
    }
|
    // Ensure all FORCE and USING variants are also explicitly defined
    FORCE INDEX LPAREN index_list RPAREN 
    {
        const char *s1 = " FORCE (", *s2 = ")";
        $$ = pool_strcat_n(3, s1, $4, s2);
        if ($$ == nullptr) YYABORT;

    }
;

index_hint_chain:
    index_hint_clause
    { $$ = $1; }
|   index_hint_chain index_hint_clause
    {
        // Concatenate multiple hints, preserving order
        $$ = pool_strcat_n(2, $1, $2);
        if ($$ == nullptr) YYABORT;
    }
;

%%

// --- Auxiliary C++ Code (Implementations) ---

// Corrected yyerror implementation order: (location, parser_context, error_message)
void yyerror(void *loc, SQL_Parser* parser, const char *s) {
    std::cerr << "❌ Parse Error: " << s << " at line " << yylineno << " col " << yycolno
         << std::endl;
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
    yycolno = 1;

    // 3. Call the parser (yyparse)
    int result = yyparse(parser);

    return result;
}

#ifdef TEST_SQL_PARSER
// ... (TEST_SQL_PARSER content removed for brevity, assuming main is external)
#endif

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
    if (c == ',') { return COMMA; }
    if (c == '*') return STAR; // FIX: '*' is the STAR wildcard/multiplication operator
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
    if (c == ';') { return SEMICOLON; }
    if (c == '.') { return DOT; }
    
    // NEW: Arithmetic tokens
    if (c == '+') { return PLUS; }
    if (c == '-') { return MINUS; } // Unary minus is handled in grammar
    if (c == '/') { return DIV; } // A lone '/' must be DIV here.

    
    // Handle @@ system variables
    if (c == '@') {
        int next_c = yygetc();
        if (next_c == '@') {
            return ATAT;
        }
        yyungetc(next_c); 
        // If it was a single @, it's not handled here
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
            std::cerr << "Unterminated backtick-quoted identifier (ascii): " << (int)c << std::endl;
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
            // Use pool_strdup (stores content without quotes)
            yylval->str = pool_strdup(buffer + 1);
            return LITERAL;
        } else {
            std::cerr << "Unterminated string literal." << std::endl; 
            return -1;
        }
    }

if (isdigit(c)) { // Start with a digit
        *p++ = c;

        // 1. Scan the rest of the integer part
        while ( (c = yygetc()) != EOF && isdigit(c) ) {
            CHECK_BUF_BOUNDS("NUMBER");
            *p++ = c;
        }

        // 2. Check for the fractional part (dot)
        if (c == '.') {
            *p++ = c; // Consume the dot

            // Scan fractional digits (must handle case like "15." or "15.00")
            // We use a separate loop variable (dot_c) for clarity
            int dot_c = yygetc();
            while (dot_c != EOF && isdigit(dot_c)) {
                CHECK_BUF_BOUNDS("NUMBER");
                *p++ = dot_c;
                dot_c = yygetc();
            }
            
            // Put back the character that terminated the fractional part (e.g., the 'W' in WHERE)
            if (dot_c != EOF) {
                yyungetc(dot_c);
            }
            
        } else {
            // If the number was an integer (no dot), put back the character
            // that terminated the integer (e.g., the space or 'W')
            if (c != EOF) {
                yyungetc(c);
            }
        }
        
        *p = '\0';
        yylval->str = pool_strdup(buffer); 
        return NUMBER;
    }    
    
    // Handle slash (must be an error if not handled as a comment already)
    if (c == '/') {
        std::cerr << "Invalid character '/' found." << std::endl;
        return -1;
    }


    // Handle keywords and IDs - ADDED BOUNDS CHECK
    // The current character 'c' has already been stored in buffer[0]
    while ( (c = yygetc()) != EOF && (isalnum(c) || c == '_' || c == '$') ) {
        CHECK_BUF_BOUNDS("ID/Keyword");
        *p++ = c;
    }
    // FIX: Only unget if not EOF
    if (c != EOF) {
        yyungetc(c);
    }
    *p = '\0';

    // Check for keywords (case-insensitive conversion to uppercase)
    std::string upper_buffer = buffer;
    
    for (char &c : upper_buffer) {
        c = static_cast<char>(toupper(c));
    }

    // --- KEYWORD CHECKS ---
    if (upper_buffer == "SELECT") return SELECT;
    if (upper_buffer == "FROM") return FROM; 
    if (upper_buffer == "WHERE") return WHERE;
    if (upper_buffer == "JOIN") return JOIN;
    if (upper_buffer == "UNION") return UNION;
    if (upper_buffer == "ALL") return ALL;
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
    
    if (upper_buffer == "IS") return IS;
    if (upper_buffer == "EXISTS") return EXISTS;
    if (upper_buffer == "NULL") return SQL_NULL;
    if (upper_buffer == "LIKE") return LIKE; 
    if (upper_buffer == "CASE") return CASE;
    if (upper_buffer == "WHEN") return WHEN;
    if (upper_buffer == "THEN") return THEN;
    if (upper_buffer == "ELSE") return ELSE;
    if (upper_buffer == "END") return END;
    if (upper_buffer == "CONVERT") return CONVERT;
    if (upper_buffer == "CAST") return CAST;
    if (upper_buffer == "UCASE") return UCASE;
    if (upper_buffer == "LOCATE") return LOCATE;
    if (upper_buffer == "CONCAT") return CONCAT;
    if (upper_buffer == "SUBSTRING") return SUBSTRING;
    if (upper_buffer == "IF") return IF;
    if (upper_buffer == "IN") return IN;
    if (upper_buffer == "LEAST") return LEAST; // Added
    if (upper_buffer == "SIGNED") return SIGNED; // Added
    if (upper_buffer == "UNSIGNED") return UNSIGNED; // Added
    if (upper_buffer == "INTEGER") return INTEGER; // Added
    if (upper_buffer == "INDEX") return INDEX;
    if (upper_buffer == "IGNORE") return IGNORE;
    if (upper_buffer == "FORCE") return FORCE;
    if (upper_buffer == "KEY") return KEY;
    if (upper_buffer == "DISTINCT") return DISTINCT;

    // Default: Must be an ID (column or table name)
    // Use pool_strdup
    yylval->str = pool_strdup(buffer);
    //fprintf(stderr, "ID=%s\n", yylval->str);
    return ID;
}
