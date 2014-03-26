/* -*-c++-*- */

#include <string.h>
#include <iostream>

enum { BUFFER_SIZE = 32 * 1024 };

class buffer_error { };

class buffer
{
  char *b;
  int size;
  int head;
  int tail;
  bool owned;
public:
  buffer(int size = BUFFER_SIZE) : size(size), head(0), tail(0) {
    b = new char[size];
    owned = true;
  }
  buffer(char *b, int size, bool owned=false)
    : b(b), size(size), head(0), tail(size), owned(owned)
  {
    if (size < 0)
      throw buffer_error();
  }
  ~buffer() { if (owned) delete[] b; }
  buffer(const buffer& a) {
    owned = true;
    size = a.size;
    head = 0;
    tail = a.used();
    b = new char[size];
    memcpy(headp(), a.headp(), a.used());
  }
  buffer(buffer&& a) {
    head = a.head;
    tail = a.tail;
    size = a.size;
    b = a.b;
    owned = a.owned;
    a.owned = false;
  }
  buffer& operator=(buffer&& a) {
    if (owned) delete[] b;
    head = a.head;
    tail = a.tail;
    size = a.size;
    b = a.b;
    owned = a.owned;
    a.owned = false;
    return *this;
  }

  // read
  int max_size() const { return size; }
  int available() const { return max_size() - used(); }
  int used() const { return tail - head; }
  bool empty() const { return used() == 0; }
  char * headp() { return b + head; }
  const char * headp() const { return b + head; }
  char * tailp() { return b + tail; }
  const char * tailp() const { return b + tail; }
  bool is(const char *a) const {
    return strlen(a) == used() && strncmp(a, headp(), used()) == 0;
  }

  // update
  void reset() { head = tail = 0; }
  char * notify_read(int n) {
    if (n > used())
      throw buffer_error();
    char *x = headp();
    head += n;
    return x;
  }
  void notify_write(int n) {
    if (n > available())
      throw buffer_error();
    tail += n;
  }
  void compact() {
    memmove(b, b + head, used());
    tail = used();
    head = 0;
  }
  void realloc(int newavail, int newhead=0) {
    int needed = newhead + used() + newavail;
    if (size < needed) {
      if (!owned)
	throw buffer_error();
      char *a = new char[needed];
      memcpy(a + newhead, headp(), used());
      delete b;
      b = a;
      tail = newhead + used();
      head = newhead;
      size = needed;
    } else if (head < newhead || available() < newavail) {
      memmove(b + newhead, headp(), used());
      tail = newhead + used();
      head = newhead;
    }
  }
  void unread(const buffer &prefix) {
    if (head < prefix.used())
      throw buffer_error();

    memcpy(headp() - prefix.used(), prefix.headp(), prefix.used());
    head -= prefix.used();
  }
  void write(const char *a, int n) {
    if (n > available())
      throw buffer_error();
    memcpy(b + tail, a, n);
    notify_write(n);
  }
  void write(const buffer &a) { write(a.headp(), a.used()); }
  buffer sub(int n) { return buffer(notify_read(n), n); }
  char * detach() {
    if (!owned)
      throw buffer_error();
    compact();
    owned = false;
    return b;
  }
};

inline bool
operator<(const buffer &a, const buffer &b)
{
  if (a.used() == b.used())
    return memcmp(a.headp(), b.headp(), a.used()) < 0;
  return a.used() < b.used();
}

inline std::ostream&
operator<<(std::ostream& o, const buffer& b)
{
  return o.write(b.headp(), b.used());
}

inline std::istream&
operator>>(std::istream &i, buffer &b)
{
  i.read(b.tailp(), b.available());
  b.notify_write((int)i.gcount());
  return i;
}
