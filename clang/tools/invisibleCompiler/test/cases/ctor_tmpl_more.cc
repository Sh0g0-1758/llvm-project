namespace ns {
struct A { template <typename T> A(T v) : x(int(v)) {} int x; };
template <typename T> struct Outer {
    struct Inner { template <typename U, typename V = int> Inner(U u) : y(sizeof(u)) {} int y; };
};
template <typename T> struct B { B(int) {} template <typename U> B(U* p) : B(0) {} };
template <typename... Ts> int count(Ts... ts) { return sizeof...(ts); }
}
int main() {
    ns::A a(2.5);
    ns::Outer<char>::Inner in(3L);
    int i = 0; ns::B<long> b(&i);
    return a.x + in.y + ns::count(1, 2, 3);
}
