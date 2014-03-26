#include <cstdint>
#include <cassert>

template<class T>
class flagged_ptr
{
private:
  uintptr_t data;

  static uintptr_t flag_mask() { return alignof(T) - 1; }
  static uintptr_t ptr_mask() { return ~flag_mask(); }
public:
  
  flagged_ptr() noexcept : data(0) { }
  flagged_ptr(std::nullptr_t _) : data(0) { }
  flagged_ptr(T *ptr) noexcept : data((uintptr_t)ptr) { }
  flagged_ptr(T *ptr, int flags) noexcept
  : data((uintptr_t)ptr | (uintptr_t)flags) {
    assert(flags == (flags & flag_mask()));
  }
  
  int get_flags() const { return data & flag_mask(); }
  bool get_flag(int flag) const { return data & flag; }
  T* get_ptr() const { return (T*)(data & ptr_mask()); }
  T& operator *() const { return *get_ptr(); }
  //operator T*() const { return get_ptr(); }
  
  bool operator==(const flagged_ptr<T>& a) const { return data == a.data; }
  bool operator!=(const flagged_ptr<T>& a) const { return data != a.data; }

};

