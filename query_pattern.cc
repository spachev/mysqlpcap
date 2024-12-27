#include "query_pattern.h"
#include <string>
#include <sstream>
#include <iostream>
#include <string.h>

Query_pattern::Query_pattern(const char* search, const char* replace):replace_str(replace)
{
  PCRE2_SIZE erroroffset;
  PCRE2_SIZE *ovector;
  int errornumber;

  if (!(re = pcre2_compile((PCRE2_SPTR) search, PCRE2_ZERO_TERMINATED | PCRE2_DOTALL,
      0, &errornumber, &erroroffset, NULL)))
  {
    std::stringstream ss;
    ss << "Invalid regular expression: " << search << ": error " << errornumber << " at offset " <<
      erroroffset << ": remainder of the string: " << search + erroroffset;
    throw Query_pattern_exception(ss.str().c_str());
  }

  pcre2_jit_compile(re, PCRE2_JIT_COMPLETE);
}

const char* Query_pattern::apply(const char* subject, size_t subject_len, char* output_buf, size_t* out_len)
{
  struct strbuf {
    char* ptr;
    strbuf(const char* s, size_t len) {
      ptr = new char[len]; // throws on error
      for (size_t i = 0; i < len; i++)
      {
        if (s[i] == '\r' || s[i] == '\n')
          ptr[i] = ' ';
        else
          ptr[i] = s[i];
      }
    }

    ~strbuf() { delete ptr;}
  } subject_buf(subject, subject_len);

  int rc =  pcre2_substitute(re, (PCRE2_SPTR)subject_buf.ptr, subject_len, 0,
                             PCRE2_SUBSTITUTE_GLOBAL | PCRE2_SUBSTITUTE_EXTENDED, 0, 0,
                             (PCRE2_SPTR)replace_str.c_str(),
                             replace_str.length(), (PCRE2_UCHAR*)output_buf, out_len);

  if (rc == PCRE2_ERROR_NOMEMORY) // likely no match thus no replacement and the query is bigger than output_buf
      return NULL;

  if (rc < 0)
  {
    std::stringstream ss;
    unsigned char err_msg[1024];
    ss << "Error applying regex for ";
    ss.write(subject, subject_len);
    
    if (pcre2_get_error_message(rc, err_msg, sizeof(err_msg)) > 0)
    {
        ss << ": " << err_msg;
    }
    
    throw Query_pattern_exception(ss.str().c_str());
  }

  if (rc == 0)
    return NULL;

  return (const char*)output_buf;
}

Query_pattern::~Query_pattern()
{
  pcre2_code_free(re);
}

#ifdef TEST_QUERY_PATTERN

struct Query_pattern_test
{
  const char* match_expr;
  const char* replace_expr;
  const char* query;

  Query_pattern_test(const char* match_expr, const char* replace_expr, const char* query):
    match_expr(match_expr), replace_expr(replace_expr), query(query)
    {
    }
};

int main(int argc, char** argv)
{
  Query_pattern_test tests[] = {
      Query_pattern_test(".*hash:\\s*(\\d+).*", "Query ID: $1", "select * from t1 /* hash: 1234 */"),
      Query_pattern_test(".*hash:\\s*(\\d+).*", "Query ID: $1", "select *\n from \n t1 /* hash: 1235 */"),
      Query_pattern_test(".*hash:\\s*(\\d+).*", "Query ID: $1", "select * from t1")
  };

  for (int i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
  {
    try
    {
      char output_buf[1024];
      size_t out_len = sizeof(output_buf);
      Query_pattern qp(tests[i].match_expr, tests[i].replace_expr);
      const char* res = qp.apply(tests[i].query, strlen(tests[i].query), output_buf, &out_len);
      printf("Query: %s\n", tests[i].query);

      if (res)
        printf("Replace result: %.*s\n", (int)out_len, output_buf);
      else
        printf("No match\n");
    }
    catch (Query_pattern_exception e)
    {
      std::cerr << "Exception: " << e.what() << std::endl;
    }
  }
}

#endif
