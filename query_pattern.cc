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

  if (!(re = pcre2_compile((PCRE2_SPTR) search, PCRE2_ZERO_TERMINATED, 0, &errornumber, &erroroffset, NULL)))
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
  int rc =  pcre2_substitute(re, (PCRE2_SPTR)subject, subject_len, 0,
                             PCRE2_SUBSTITUTE_GLOBAL | PCRE2_SUBSTITUTE_EXTENDED, 0, 0,
                             (PCRE2_SPTR)replace_str.c_str(),
                             replace_str.length(), (PCRE2_UCHAR*)output_buf, out_len);
  if (rc < 0)
  {
    std::stringstream ss;
    ss << "Error applying regex for " << subject;
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

int main(int argc, char** argv)
{
  try
  {
    char output_buf[1024];
    Query_pattern qp(".*hash:\\s*(\\d+).*", "Query ID: $1");
    const char* subject = "select * from t1 /* hash: 1234 */";
    size_t out_len = sizeof(output_buf);
    const char* res = qp.apply(subject, strlen(subject), output_buf, &out_len);

    if (res)
      printf("%.*s\n", (int)out_len, output_buf);
    else
      printf("No match\n");
  }
  catch (Query_pattern_exception e)
  {
    std::cerr << "Exception: " << e.what() << std::endl;
  }
}

#endif
