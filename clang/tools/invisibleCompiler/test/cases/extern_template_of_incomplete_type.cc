// The explicit instantiation declarations are dropped so that decl-before-def can specialize
// what they name, except those whose instantiation can't be in this translation unit: `os` is
// only declared, as with doctest's forward declared std::basic_ostream.
template <class C> struct os;
template <class C> os<C> &insert(os<C> &o, const C *s) { return o.write(s); }
extern template os<char> &insert(os<char> &, const char *);
template <class T> struct box { T t; T get() const { return t; } };
extern template struct box<int>;
os<char> &print(os<char> &o) { return insert(o, "x"); }
int main() { box<int> b{3}; return b.get() - 3; }
