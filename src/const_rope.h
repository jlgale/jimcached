struct mem;

class const_rope
{
private:
  const mem *head_;
  const mem *tail_;
public:
  const_rope() : const_rope(nullptr, nullptr) { }
  const_rope(const mem *head, const mem *tail) : head_(head), tail_(tail) { }
  
  size_t size() const;
  uint64_t hash(uint64_t seed) const;
  const mem *pop();
};
