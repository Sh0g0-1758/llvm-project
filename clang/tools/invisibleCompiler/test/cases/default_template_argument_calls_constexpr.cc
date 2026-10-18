// A default template argument calling a constexpr function, as kakoune's
// `template <typename T, MemoryDomain domain = memory_domain(Meta::Type<T>{})>`, is evaluated
// where a member's type names the template, before what is deferred: the functions an
// unresolved call outside a body names stay where they are written.
enum class Domain { Plain, Wide };
template <class T> struct Type {};
constexpr Domain domain(Type<int>) { return Domain::Plain; }
constexpr Domain domain(Type<double>) { return Domain::Wide; }
template <class T, Domain d = domain(Type<T>{})> struct Vec {
  int get() const { return int(d); }
};
struct Holder { Vec<double> wide; Vec<int> plain; };
int main() {
  Holder h;
  return h.wide.get() + h.plain.get() - 1;
}
