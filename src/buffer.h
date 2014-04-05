/* -*-c++-*- */

#include <cstring>
#include <iostream>
#include <cassert>

class buf
{
 private:
  const char *head_;
  const char *tail_;
 public:
  buf() : buf(nullptr, nullptr) { }
  buf(const char *head, const char *tail) : head_(head), tail_(tail) { }
  buf(const char *b, size_t size) : head_(b), tail_(b + size) { }
  size_t size() const { return tail_ - head_ ; }
  size_t used() const { return size(); }
  bool empty() const { return size() == 0; }
  const char *headp() const { return head_; }
  const char *tailp() const { return tail_; }
  bool is(const char *a) const {
    return strncmp(a, headp(), size()) == 0 && a[size()] == '\0';
  }
  const char * notify_read(int n) {
    assert(n <= size());
    const char *x = headp();
    head_ += n;
    return x;
  }
  buf sub(int n) { return buf(notify_read(n), n); }
};

inline std::ostream&
operator<<(std::ostream& o, const buf& b)
{
  return o.write(b.headp(), b.size());
}

class buffer_error { };

class buffer
{
  char *b;
  int size;
  int head;
  int tail;
public:
  buffer(int size) : size(size), head(0), tail(0) {
    b = new char[size];
  }
  ~buffer() { if (b) delete[] b; }
  buffer(const buffer& a) {
    size = a.size;
    head = 0;
    tail = a.used();
    b = new char[size];
    memcpy(headp(), a.headp(), a.used());
  }
  buffer(const buf& a) {
    size = a.size();
    head = 0;
    tail = a.size();
    b = new char[size];
    memcpy(headp(), a.headp(), a.size());
  }
  buffer(buffer&& a) {
    head = a.head;
    tail = a.tail;
    size = a.size;
    b = a.b;
    a.b = nullptr;
    a.size = 0;
  }
  buffer& operator=(buffer&& a) {
    if (b)
      delete[] b;
    head = a.head;
    tail = a.tail;
    size = a.size;
    b = a.b;
    a.b = nullptr;
    a.size = 0;
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
    return strncmp(a, headp(), used()) == 0 && a[used()] == '\0';
  }
  operator buf () const { return buf(headp(), used()); }

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
  void write(const char *a, int n) {
    if (n > available())
      throw buffer_error();
    memcpy(b + tail, a, n);
    notify_write(n);
  }
  void write(const buffer &a) { write(a.headp(), a.used()); }
  buf sub(int n) { return buf(notify_read(n), n); }
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
