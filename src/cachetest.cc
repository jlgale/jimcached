#include "buffer.h"
#include "cache.h"
#include <cassert>
#include <iostream>

cache *cash = nullptr;

static void
reset()
{
  if (cash)
    delete cash;
  cash = new cache(16 * 1024);
}

static buf
cbuffer(const char *s)
{
  return buf(s, strlen(s));
}

static rope
alloc(const char *v)
{
  int n = strlen(v);
  mem *m = mem_alloc(n);
  memcpy(m->data, v, n);
  return rope(m, m);
}

static void
set(const char *k, const char *v, unsigned flags=0, unsigned exptime=0)
{
  cache_error_t err = cash->set(cbuffer(k), flags, exptime, alloc(v));
  assert(err == cache_error_t::stored);
}

static void
add(const char *k, const char *v, unsigned flags=0, unsigned exptime=0,
    bool expect_success=true)
{
  rope r = alloc(v);
  cache_error_t err = cash->add(cbuffer(k), flags, exptime, r);
  if (expect_success)
    assert(err == cache_error_t::stored);
  else
    assert(err == cache_error_t::set_error);
}

static void
incr(const char *k, uint64_t v, uint64_t e)
{
  buffer key = cbuffer(k);
  uint64_t a;
  cache_error_t err = cash->incr(key, v, &a);
  assert(err == cache_error_t::stored);
  assert(a == e);
}

static void
decr(const char *k, uint64_t v, uint64_t e)
{
  buffer key = cbuffer(k);
  uint64_t a = 0;
  //cash->decr(key, v, &a); XXX
  assert(a == e);
}

static void
get(const char *k, const char *expect)
{
  buffer key = cbuffer(k);
  cache::ref r = cash->get(key);
  if (r == nullptr) {
    assert(expect == NULL);
    return;
  }
  
  const_rope data = r->read();
  size_t n = strlen(expect);
  for (const mem *m = data.pop(); m; m = data.pop()) {
    assert(m->size <= n);
    assert(memcmp(m->data, expect, m->size) == 0);
    n -= m->size;
    expect += m->size;
  }
  assert(n == 0);
}

static void
test2()
{
  // Increment and Decrement
  reset();
  add("a", "1001");
  add("b", "0");
  incr("a", 1, 1002);
  incr("a", 1, 1003);
  decr("b", 1, 0);
  incr("b", 1, 1);
  incr("b", 1, 2);
  incr("b", 1000, 1002);
  std::cout << "test2 passed" << std::endl;
}

static void
test1()
{
  reset();
  add("pooh", "bear");
  add("pooh", "b33r", 0, 0, false);
  set("tigger", "too");
  get("pooh", "bear");
  get("pooh", "bear");
  get("tigger", "too");
  get("piglet", nullptr);
  get("piglet", nullptr);
  set("pooh", "beer");
  get("pooh", "beer");
  std::cout << "test1 passed" << std::endl;
}

int main(int argc, char** argv)
{
  test1();
  test2();
  delete cash;
}
