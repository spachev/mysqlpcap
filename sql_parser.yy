// File: sql_parser.yy

// --- CODE FOR SOURCE FILE (.cc) PREAMBLE ---
%code {
#include <iostream>
#include <string>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <sstream> 
#include <algorithm> 

// Global variable for line number 
int yylineno = 1;

// Global variable for the mock lexer 
char *yytext;

// --- Input Buffer Management Globals ---
const char* yy_input_buffer = nullptr;
const char* yy_current_ptr = nullptr;

// Function to read the next character from the in-memory buffer, replacing getchar()
int yygetc() {
    if (*yy_current_ptr == '\0') {
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
// ADDED AS token
%token AS
// New tokens for DML/DDL
%token INSERT INTO VALUES UPDATE SET DELETE ALTER TABLE ADD COLUMN DROP CHANGE MODIFY
%token COMMA STAR LPAREN RPAREN
%token SEMICOLON 

// Simple Value Tokens
%token <str> ID      
%token <str> LITERAL 
%token <str> NUMBER  // CHANGED: Now returns <str> to handle floating-point

// Comparator Tokens
%token EQ LT GT LE GE NE

// Non-Terminal Symbols
%type <str> query statement select_stmt column_list table_list table_ref join_list join_clause optional_join_type optional_where condition
// New non-terminals
%type <str> insert_stmt update_stmt delete_stmt alter_stmt alter_action value value_list set_list
// Added non-terminals for new syntax features
%type <str> column_expr optional_as_alias comma_separated_table_list optional_insert_columns column_id_list


// --- Operator Precedence ---
%left COMMA
%left EQ NE LT GT LE GE

%%

// --- Grammar Rules ---

// 1. Main entry point (Top level rule to handle optional semicolon)
query:
    statement
    { $$ = $1; } 
|   statement SEMICOLON
    { $$ = $1; } 
;

// 1a. SQL Statement body (renamed from the old 'query')
statement:
    select_stmt
|   insert_stmt
|   update_stmt
|   delete_stmt
|   alter_stmt
    {
        $$ = $1; // Pass up the semantic value of the matched statement
    }
;

// 2. Structure of a SELECT statement
select_stmt:
    SELECT column_list FROM table_list optional_where
    {
    }
;

// 3. Handling the table list, including joins AND comma-separated tables
table_list:
    table_ref comma_separated_table_list join_list
    {
        // $1 is the first table, $2 is the comma list, $3 is the join list.
        char *temp = (char*)malloc(strlen($1) + strlen($2) + strlen($3) + 5);
        sprintf(temp, "%s%s%s", $1, $2, $3); 
        $$ = temp;
    }
;

// NEW: Handles the comma-separated list of tables (implicit join)
comma_separated_table_list:
    /* empty */
    { $$ = strdup(""); }
|   comma_separated_table_list COMMA table_ref
    {
        // $3 is the table ref (which includes alias info)
        char *temp = (char*)malloc(strlen($1) + strlen($3) + 4);
        sprintf(temp, "%s, %s", $1, $3); 
        $$ = temp;
    }
;

// 4. Handling the primary table reference and potential alias
table_ref:
    ID optional_as_alias 
    {
        // $1 is the table name, $2 is the alias (or empty string/alias string).
        // Call the handler with the base table name and the main query type (SELECT).
        parser->handle_table("SELECT", $1);
        char *temp = (char*)malloc(strlen($1) + strlen($2) + 2);
        sprintf(temp, "%s%s", $1, $2);
        $$ = temp;
    }
|   LPAREN ID RPAREN optional_as_alias // FIX: Added support for (table_id) reference
    {
        // $2 is the table name, $4 is the alias
        // Call the handler with the base table name and the main query type (SELECT).
        parser->handle_table("SELECT", $2);
        char *temp = (char*)malloc(strlen($2) + strlen($4) + 6);
        // Format as (table_name) alias_string
        sprintf(temp, "(%s)%s", $2, $4);
        $$ = temp;
    }
;

// NEW: Handles both implicit and explicit aliases
optional_as_alias:
    /* empty */
    { $$ = strdup(""); }
|   AS ID // Explicit AS alias
    { 
        char *temp = (char*)malloc(strlen($2) + 5);
        sprintf(temp, " AS %s", $2);
        $$ = temp;
    }
|   ID // Implicit alias
    { 
        char *temp = (char*)malloc(strlen($1) + 2);
        sprintf(temp, " %s", $1);
        $$ = temp;
    }
;

// 5. Handling zero or more explicit joins
join_list:
    /* empty */
    {
        $$ = strdup("");
        
    }
|   join_list join_clause
    {
        char *temp = (char*)malloc(strlen($1) + strlen($2) + 2);
        sprintf(temp, "%s %s", $1, $2); 
        $$ = temp;
    }
;
// 7. Optional join type keywords (e.g., INNER, LEFT, RIGHT)
optional_join_type:
    /* empty */
    { $$ = strdup("");
        
    }
|   INNER
    { $$ = strdup("INNER "); }
|
    LEFT
    { $$ = strdup("LEFT "); 
    }
|
    RIGHT
    { $$ = strdup("RIGHT "); }
;

// 6. Structure of a single join clause
join_clause:
    optional_join_type JOIN table_ref ON condition
    {
        // FIX: Replaced ID with table_ref for robustness.
        // table_ref already calls parser->handle_table("SELECT", table_name) with the correct query type.
        char *temp = (char*)malloc(strlen($1) + strlen($3) + strlen($5) + 20);
        sprintf(temp, "%sJOIN %s ON %s", $1, $3, $5);
        
        $$ = temp;
    }
;

// 8. Handling the column list (now uses column_expr)
column_list:
    column_expr
    { $$ = $1; }
|
    column_list COMMA column_expr
    {
        char *temp = (char*)malloc(strlen($1) + strlen($3) + 3);
        sprintf(temp, "%s, %s", $1, $3); 
        $$ = temp;
    }
;

// NEW: Supports column or function expression
column_expr:
    STAR
    { $$ = strdup("*"); }
|   ID
    { $$ = $1; } 
|   ID LPAREN STAR RPAREN // Function call like COUNT(*)
    {
        char *temp = (char*)malloc(strlen($1) + 5);
        sprintf(temp, "%s(*)", $1);
        $$ = temp;
    }
;

// 9. Handling the optional WHERE clause
optional_where:
    /* empty */
    { $$ = strdup("");
        
    }
|   WHERE condition
    {
        char *temp = (char*)malloc(strlen($2) + 8);
        sprintf(temp, "WHERE %s", $2); 
        $$ = temp;
    }
;
// 10. Handling a simple condition (Updated to use generic `value` non-terminal)
condition:
    ID EQ value
    {
        char *temp = (char*)malloc(strlen($1) + strlen($3) + 7);
        sprintf(temp, "%s = %s", $1, $3); 
        $$ = temp;
    }
|
    ID GT value
    {
        char *temp = (char*)malloc(strlen($1) + strlen($3) + 7);
        sprintf(temp, "%s > %s", $1, $3); 
        $$ = temp;
    }
|
    ID LT value
    {
        char *temp = (char*)malloc(strlen($1) + strlen($3) + 7);
        sprintf(temp, "%s < %s", $1, $3); 
        $$ = temp;
    }
|
    ID EQ ID // For column = column comparison
    {
        char *temp = (char*)malloc(strlen($1) + strlen($3) + 4);
        sprintf(temp, "%s = %s", $1, $3); 
        $$ = temp;
    }
;

// --- DML/DDL Rules ---

// 11. INSERT Statement: INSERT INTO table_name optional_column_list VALUES (value, ...)
insert_stmt:
    INSERT INTO ID optional_insert_columns VALUES LPAREN value_list RPAREN
    {
        // $3 is the table name
        parser->handle_table("INSERT", $3);
        // Return a simplified string representation
        char *temp = (char*)malloc(strlen($3) + 40);
        sprintf(temp, "INSERT INTO %s%s VALUES (...)", $3, $4);
        $$ = temp;
    }
;

// NEW: Handles the optional column list in INSERT
optional_insert_columns:
    /* empty */
    { $$ = strdup(""); }
|   LPAREN column_id_list RPAREN
    {
        // $2 is the list of IDs (columns)
        char *temp = (char*)malloc(strlen($2) + 5);
        sprintf(temp, " (%s)", $2);
        $$ = temp;
    }
;

// NEW: A comma-separated list of column IDs
column_id_list:
    ID
    { $$ = $1; }
|   column_id_list COMMA ID
    {
        char *temp = (char*)malloc(strlen($1) + strlen($3) + 3);
        sprintf(temp, "%s, %s", $1, $3); 
        $$ = temp;
    }
;


// 12. UPDATE Statement: UPDATE table_name SET col = val WHERE condition
update_stmt:
    UPDATE ID SET set_list optional_where
    {
        // $2 is the table name
        parser->handle_table("UPDATE", $2);
        // Return a simplified string representation
        char *temp = (char*)malloc(strlen($2) + strlen($5) + 30);
        sprintf(temp, "UPDATE %s SET (...) %s", $2, $5);
        $$ = temp;
    }
;

// 13. DELETE Statement: DELETE FROM table_name WHERE condition
delete_stmt:
    DELETE FROM ID optional_where
    {
        // $3 is the table name
        parser->handle_table("DELETE", $3);
        // Return a simplified string representation
        char *temp = (char*)malloc(strlen($3) + strlen($4) + 20);
        sprintf(temp, "DELETE FROM %s %s", $3, $4);
        $$ = temp;
    }
;

// 14. ALTER TABLE Statement: ALTER TABLE table_name alter_action
alter_stmt:
    ALTER TABLE ID alter_action
    {
        // $3 is the table name
        parser->handle_table("ALTER", $3);
        // Return a simplified string representation
        char *temp = (char*)malloc(strlen($3) + 20);
        sprintf(temp, "ALTER TABLE %s (...)", $3);
        $$ = temp;
    }
;

// Helper rule for a list of values (simplified)
value_list:
    value
|   value_list COMMA value
    {
        // No complex string concatenation here, just chaining
    }
;

// Helper rule for a single value in INSERT/SET (Simplified as all are now <str>)
value:
    ID 
|   LITERAL 
|   NUMBER // Number is now <str> (including floats)
    { 
        $$ = $1; 
    }
;

// Helper rule for SET list (col = val)
set_list:
    ID EQ value
|   set_list COMMA ID EQ value
;

// Helper rule for ALTER actions (simplified)
alter_action:
    ADD COLUMN ID ID        // ADD COLUMN col_name col_type (col_type simplified to ID)
    {
        char *temp = (char*)malloc(strlen($3) + strlen($4) + 15);
        sprintf(temp, "ADD COLUMN %s %s", $3, $4);
        $$ = temp;
    }
|   DROP COLUMN ID
    {
        char *temp = (char*)malloc(strlen($3) + 15);
        sprintf(temp, "DROP COLUMN %s", $3);
        $$ = temp;
    }
|   CHANGE COLUMN ID ID ID  // CHANGE COLUMN old_col_name new_col_name new_col_type
    {
        char *temp = (char*)malloc(strlen($3) + strlen($4) + strlen($5) + 20);
        sprintf(temp, "CHANGE COLUMN %s to %s", $3, $4);
        $$ = temp;
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
    // 1. Reset the buffer pointers and line number
    yy_input_buffer = query;
    yy_current_ptr = query;
    yylineno = 1;

    // 2. Call the parser (yyparse)
    return yyparse(parser);
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
    // Queries that previously failed, now passing
    const char* tests[] = {
        "SELECT u.name FROM users AS u, posts p WHERE u.id = p.user_id;",
        "SELECT u.name FROM users u0, posts p0 WHERE u.id = p.user_id",
        "INSERT INTO new_users (name) VALUES ('John');",
        "UPDATE products SET price = 15.00 WHERE id = 10;",
        "DELETE FROM old_logs WHERE date < '2023-01-01';",
        "SELECT count(*) FROM employees;",
        // Test case that previously failed due to parenthesized table name
        "SELECT * FROM (table1) JOIN table2 ON table1.id = table2.id;", 
        // Test case that previously called handle_table with "JOIN" for table2
        "SELECT * FROM table1 JOIN table2 ON table1.id = table2.id;",
        "SELECT * FROM `table1` JOIN table2 ON table1.id = table2.id;"
    };

    std::cout << "--- Rerunning tests with corrected handler logic ---" << std::endl;
    for (const char* query : tests) {
        std::cout << "\nParsing: " << query << std::endl;
        Demo_Parser parser;
        if (yyparse_string(&parser, query, strlen(query)) == 0) {
            std::cout << "Success parsing: " << query << std::endl;
        } else {
            std::cout << "Failed to parse: " << query << std::endl;
        }
    }

    return 0;
}

#endif

// ** MOCK LEXER (yylex) **
int yylex (void *yylval_ptr, void *yyloc_ptr, SQL_Parser* parser) {
    // Cast the void pointer to the expected union type
    YYSTYPE* yylval = (YYSTYPE*)yylval_ptr;
    
    // A small buffer to hold the current token's characters
    static char buffer[256];
    int c;
    char *p = buffer;

    // Skip whitespace
    while ( (c = yygetc()) != EOF ) {
        if (!isspace(c)) {
            break;
        }
    }

    if (c == EOF) return 0;

    // Handle single-character tokens first
    if (c == ',') return COMMA;
    if (c == '*') return STAR;
    if (c == '=') return EQ;
    if (c == '<') {
        c = yygetc();
        if (c == '=') return LE;
        yyungetc(c);
        return LT;
    }
    if (c == '>') {
        c = yygetc();
        if (c == '=') return GE;
        yyungetc(c);
        return GT;
    }
    if (c == '(') return LPAREN;
    if (c == ')') return RPAREN;
    if (c == ';') return SEMICOLON; 

    *p++ = c;
    
    // Handle backtick-quoted IDs (e.g., `id`) - New
    if (c == '`') {
        while ( (c = yygetc()) != EOF && c != '\n' && c != '`' ) {
            *p++ = c;
        }
        if (c == '`') {
            *p = '\0';
            yylval->str = strdup(buffer + 1);
            return ID;
        } else {
            std::cerr << "Unterminated backtick-quoted identifier." << std::endl;
            return -1;
        }
    }

    // Handle string literals (e.g., 'text')
    if (c == '\'') {
        while ( (c = yygetc()) != EOF && c != '\n' && c != '\'' ) {
            *p++ = c;
            
        }
        if (c == '\'') {
            *p = '\0';
            yylval->str = strdup(buffer + 1);
            // Content inside quotes
            return LITERAL;
            
        } else {
            // Keep error message in cerr
            std::cerr << "Unterminated string literal." << std::endl; 
            // We return -1 which is typically not a valid token number, causing the parser to fail
            return -1;
        }
    }

    // Handle numbers (integers or floats) - Updated
    if (isdigit(c)) {
        bool has_dot = false;
        while ( (c = yygetc()) != EOF ) {
            if (isdigit(c)) {
                *p++ = c;
            } else if (c == '.' && !has_dot) {
                *p++ = c;
                has_dot = true;
            } else {
                break;
            }
        }
        yyungetc(c);
        *p = '\0';
        yylval->str = strdup(buffer); // Return as string
        return NUMBER; 
    }

    // Handle keywords and IDs
    while ( (c = yygetc()) != EOF && (isalnum(c) || c == '_' || c == '.') ) {
        *p++ = c;
        
    }
    yyungetc(c);
    // Put back the non-alphanumeric character
    *p = '\0';
    yytext = buffer;
    

    // Check for keywords (case-insensitive conversion to uppercase)
    std::string upper_buffer = buffer;
    
    for (char &c : upper_buffer) {
        c = static_cast<char>(toupper(c));
        
    }

    if (upper_buffer == "SELECT") return SELECT;
    if (upper_buffer == "FROM") return FROM; 
    if (upper_buffer == "WHERE") return WHERE;
    if (upper_buffer == "JOIN") return JOIN;
    if (upper_buffer == "ON") return ON;
    if (upper_buffer == "INNER") return INNER;
    if (upper_buffer == "LEFT") return LEFT;
    if (upper_buffer == "RIGHT") return RIGHT; 
    if (upper_buffer == "AS") return AS; // New Keyword

    // New DML/DDL Keywords
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
    
    // Default: Must be an ID (column or table name)
    yylval->str = strdup(buffer);
    return ID;
}
