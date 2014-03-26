class rope
{
private:
  mem * const head_;
  mem * const tail_;
public:
  rope(mem *head, mem *tail) : head_(head), tail_(tail) { }
  rope() : rope(nullptr, nullptr) { }
  size_t size() const { return mem_size(head_, tail_); }

  mem *head() const { return head_; }
  mem *tail() const { return tail_; }
};

