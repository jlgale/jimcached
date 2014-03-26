#include <ctime>
#include <cassert>
#include <cstdio>

#include "murmur2.h"
#include "mem.h"
#include "rope.h"
#include "entry.h"

namespace {
  thread_local int updated_atime = 0;
  constexpr int update_atime_every = 8;
}

entry::~entry()
{
  assert(deleted == false);
  deleted = true;
  mem_free(data.head);
}

void
entry::append(const rope &a)
{
  mem *old = data.tail.exchange(a.tail());
  assert(old->next == nullptr);
  old->next = a.head();
  mtime.update();
}

void
entry::prepend(const rope &p)
{
  mem *old = data.head;
  do {
    // XXX - backoff?
    p.tail()->next = old;
  } while (!data.head.compare_exchange_weak(old, p.head()));
  mtime.update();
}

static const mem *
mem_consume_whitespace(const mem *head, const mem *tail,
                       mem::size_t i, mem::size_t *i_out)
{
  for (; i < head->size; ++i) {
    switch (head->data[i]) {
    case ' ':
    case '\t':
    case '\r':
    case '\n':
      continue;
    default:
      *i_out = i;
      return head;
    }
  }
  if (head == tail) {
    *i_out = i;
    return head;
  }
  return mem_consume_whitespace(const_cast<const mem*>(head->next),
                                tail, 0, i_out);
}

static uint64_t
mem_atoi_r(const mem *head, const mem *tail, uint64_t a, mem::size_t i)
{
  for(; i < head->size; ++i) {
    switch (head->data[i]) {
    case '0' ... '9':
      a = a * 10 + head->data[i] - '0';
      continue;
    default:
      head = mem_consume_whitespace(head, tail, i, &i);
      if (head != tail || i != tail->size)
        throw "not a number";   // XXX
      return a;
    }
  }
  if (head == tail)
    return a;

  return mem_atoi_r(head->next, tail, a, 0);
}

static uint64_t
mem_atoi(const mem *head, const mem *tail)
{
  mem::size_t i;
  head = mem_consume_whitespace(head, tail, 0, &i);
  return mem_atoi_r(head, tail, 0, i);
}

static bool
cmpxchg128(__int128 *a, __int128 b, __int128 c)
{
  return __sync_bool_compare_and_swap(a, b, c);
}

enum { max_incr_size = 32 };      // XXX real max
uint64_t
entry::incrdecr(std::function<uint64_t (uint64_t )> doit)
{
  mem *b = mem_alloc(max_incr_size); // XXX - free b on exception
  struct { mem *head, *tail; } n = { b, b };

  uint64_t a;
  struct { mem *head, *tail; } p;
  do {
    // XXX - backoff?
    p.head = data.head;
    p.tail = data.tail;
    a = mem_atoi(p.head, p.tail); // XXX - head and tail might be
                                  // disconnected
    a = doit(a);
    b->size = snprintf(b->data, max_incr_size, "%lu", a);
    assert(b->size < max_incr_size);
  } while(!cmpxchg128((__int128*)&data, *(__int128*)&p, *(__int128*)&n));
  mem_free(p.head);
  
  mtime.update();
  return a;
}

uint64_t
entry::incr(uint64_t v)
{
  return incrdecr([&](uint64_t a) { return a + v; });
}

uint64_t
entry::decr(uint64_t v)
{
  return incrdecr([&](uint64_t a) { return (a > v) ? a - v : 0; });
}

bool
entry::expired() const
{
    if (exptime) {
      time_t now;
      time(&now);
      return exptime <= now;
    }
    return false;
}

const_rope
entry::read()
{
  mem *head = data.head;
  if (updated_atime++ % update_atime_every == 0)
    atime.update();
  return const_rope(head, mem_tail(head));
}

bool
entry::cas(uint32_t newflags, uint32_t newexptime,
           uint64_t version, const rope &r)
{
  const_rope cur = const_rope(data.head, data.tail);
  uint64_t cur_version = cur.hash(flags);
  assert(sizeof(cur) == sizeof(data));

  if (cur_version == version &&
      cmpxchg128((__int128*)&data, *(__int128*)&cur, *(__int128*)&r)) {
    flags = newflags;
    exptime = newexptime;
    mtime.update();
    return true;
  } else {
    return false;
  }
}

void
entry::touch(uint32_t exptime)
{
  this->exptime = exptime;
  mtime.update();
}

size_t
entry::size() const
{
  return mem_size(data.head, nullptr);
}
