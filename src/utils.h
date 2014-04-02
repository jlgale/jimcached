/* -*-c++-*- */

#include <memory>
#define CRLF "\r\n"

buffer consume_token(buffer &b);
bool consume_int(buffer &b, unsigned long *i);
bool consume_u64(buffer &b, uint64_t *i);
int find_end_of_command(const char *buf, int len);
