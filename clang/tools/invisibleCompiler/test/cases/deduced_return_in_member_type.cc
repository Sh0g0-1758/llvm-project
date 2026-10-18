// A specialization with a deduced return type that a class's member type names, through
// decltype, is defined before what is written at the end declares a member of the class: libstdc++'s
// insert_iterator<C>::_Iter is decltype(ranges::__begin(...)) (kakoune/highlighters).
template <class T> auto first(T &t) { return t.items[0]; }
template <class C> struct cursor {
  using It = decltype(first(*(C *)nullptr));
  C *c;
  It get() const { return first(*c); }
};
struct Line { int items[2] = {3, 5}; };
int main() {
  Line l;
  cursor<Line> at{&l};
  return at.get() - 3;
}
