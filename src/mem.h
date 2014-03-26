#define MEM_MAGIC (0xabcd1234)

#include <iostream>

struct mem
{
  typedef int32_t size_t;
  uint32_t magic;
  mem *next;
  size_t size;
  char data[0];
};

mem * mem_tail(mem *head);
const mem * mem_tail(const mem *head);
mem * mem_alloc(size_t size);
void mem_free(mem *m);
size_t mem_size(const mem *head, const mem *tail);

inline std::ostream&
operator<<(std::ostream& o, const mem& m)
{
  return o.write(m.data, m.size);
}

