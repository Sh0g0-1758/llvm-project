namespace a {
struct Bag { int d[2] = {4, 5}; };
template <class T> int* begin(T& x) { return x.d; }
template <class T> int* end(T& x) { return x.d + 2; }
}
namespace c {
template <class T> int* begin(T&) { return nullptr; }
template <class T> int* end(T&) { return nullptr; }
int f(a::Bag& bag) { int s = 0; for (int x : bag) s += x; return s; }
}
int main() { a::Bag b; return c::f(b); }
