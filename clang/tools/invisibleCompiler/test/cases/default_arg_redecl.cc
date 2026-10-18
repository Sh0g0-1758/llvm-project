int g_limit = 4;
void f(int x = 1);
void f(int x);
void f(int x) {}
struct A {
    void m(int a, int b = 2);
    template <class T> T t(T v = T{});
    A(int x = g_limit) {}
};
void A::m(int a, int b) {}
template <class T> T A::t(T v) { return v; }
template <class T> struct B { void n(T v = T()); };
template <class T> void B<T>::n(T v) {}
int main() { f(); A a; a.m(1); a.t<int>(); B<long> b; b.n(); }
