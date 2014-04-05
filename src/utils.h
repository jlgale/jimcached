/* -*-c++-*- */

#include <memory>
#define CRLF "\r\n"

buf consume_token(buf &b);
bool consume_int(buf &b, unsigned long *i);
bool consume_u64(buf &b, uint64_t *i);
const char *find_end_of_command(const char *buf, int len);
