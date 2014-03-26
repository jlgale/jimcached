#include <cstddef>
#include <cstdlib>
#include <cassert>
#include <cstdint>
#include "mem.h"

mem *
mem_tail(mem *head)
{
  mem *next = head->next;
  if (next)
    return mem_tail(next);
  return head;
}

const mem *
mem_tail(const mem *head)
{
  const mem *next = head->next;
  if (next)
    return mem_tail(next);
  return head;
}

mem *
mem_alloc(size_t size)
{
  mem *b = static_cast<mem *>(malloc(sizeof(struct mem) + size));
  b->magic = MEM_MAGIC;
  b->next = nullptr;
  b->size = (uint32_t)size;
  return b;
}

void
mem_free_now(mem *m)
{
  assert(m->magic == MEM_MAGIC);
  while (m) {
    mem *next = m->next;
    free(m);
    m = next;
  }
}

void
mem_free(mem *m)
{
  // XXX
  mem_free_now(m);
}

size_t
mem_size(const mem *head, const mem *tail)
{
  if (head == tail)
    return head ? head->size : 0;
  return head->size + mem_size(head->next, tail);
}
