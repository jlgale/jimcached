/* const_rope - a linked list of memory buffers.
 *
 * Used when reading from the cache. The cache gives us a list of
 * buffers which represent the value of the object. Be consist in
 * reads (for GETS, etc) we remember not only the head of the linked
 * list but also the tail.  Const rope is basically this head/tail
 * pair.
 */
struct mem;

class const_rope_iter;

class const_rope
{
private:
  const mem *head_;
  const mem *tail_;
public:
  const_rope() : const_rope(nullptr, nullptr) { }
  const_rope(const mem *head, const mem *tail)
    : head_(head), tail_(tail) { }

  size_t size() const;
  uint64_t hash(uint64_t seed) const;
  const mem *pop();
  const mem *head() const { return head_; }
};
