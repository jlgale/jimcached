#include <cstddef>
#include <cstdint>
#include "mem.h"
#include "const_rope.h"
#include "murmur2.h"

size_t const_rope::size() const
{
  size_t s = head_->size;
  for (const mem *m = head_; m != tail_; m = m->next)
    s += m->next->size;
  return s;
}

uint64_t const_rope::hash(uint64_t seed) const
{
  // XXX - MurmurHash64B is optimized for 32-bit systems
  // XXX - We really want to use an incremental hash
  uint64_t hash = MurmurHash64A(head_->data, head_->size, seed);
  
  for (const mem *m = head_; m != tail_; m = m->next)
    hash = MurmurHash64A(m->next->data, m->next->size, hash);
  return hash;
}

const mem *const_rope::pop()
{
  const mem *r = head_;
  if (r == tail_) {
    head_ = nullptr;
    tail_ = nullptr;
  } else {
    head_ = head_->next;
  }
  return r;
}
