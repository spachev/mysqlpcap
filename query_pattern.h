#ifndef QUERY_PATTERN_H
#define QUERY_PATTERN_H

#include <stdexcept>
#include <string>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>


class Query_pattern_exception: public std::runtime_error
{
public:
  Query_pattern_exception(const char* msg): std::runtime_error(msg)
  {
  }
};

class Query_pattern
{
protected:
  pcre2_code* re;
  std::string replace_str;

public:
  Query_pattern(const char* search, const char* replace);
  ~Query_pattern();

  const char* apply(const char* subject, size_t subject_len, char* output_buf, size_t* out_len);

};

#endif
