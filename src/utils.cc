#include "buffer.h"
#include "utils.h"

#include <cstring>

static void
consume_whitespace(buf &b)
{
  while (!b.empty()) {
    switch (*b.headp()) {
    case ' ':
    case '\t':
      b.notify_read(1);
      continue;
    default:
      return;
    }
  }
}

buf
consume_token(buf &b)
{
  consume_whitespace(b);
  const char *start = b.headp();
  const char *end = start + strcspn(start, " \t\n\r");
  return b.sub((int)(end - start));
}

static bool
is_whitespace(char c)
{
  switch (c) {
  case ' ':
  case '\n':
  case '\r':
  case '\t':
    return true;
  }
  return false;
}

static bool
is_terminal(char c)
{
  return c == '\0' || is_whitespace(c);
}

bool
consume_int(buf &b, unsigned long *i)
{
  consume_whitespace(b);
  char *end;
  unsigned long v = strtoul(b.headp(), &end, 10);
  if (b.headp() == end || !is_terminal(*end))
    return false;
  b.notify_read((int)(end - b.headp()));
  *i = v;
  return true;
}

bool
consume_u64(buf &b, uint64_t *i)
{
  consume_whitespace(b);
  char *end;
  unsigned long v = strtoull(b.headp(), &end, 10);
  if (b.headp() == end || !is_terminal(*end))
    return false;
  b.notify_read((int)(end - b.headp()));
  *i = v;
  return true;
}

const char *
find_end_of_command(const char *buf, int len)
{
  const char *found = (char *)memmem(buf, len, CRLF, strlen(CRLF));
  return found ? found + strlen(CRLF) : nullptr;
}
