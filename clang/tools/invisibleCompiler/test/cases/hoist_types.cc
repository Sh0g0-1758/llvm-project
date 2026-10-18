struct V { int n; }; namespace ns { typedef unsigned long len_t; using ::V; template <class T> struct Box { T t; }; }
struct S {
    typedef ns::V view_type;
    template <class T> using box = ns::Box<T>;
    operator view_type() const { return ns::V{1}; }
    view_type get() const { return ns::V{2}; }
    ns::Box<view_type> boxed() const { return {}; }
    box<int> aliased() const { return {}; }
};
template <class T> struct W { typedef T value_type; value_type get() const { return T{}; } };
ns::len_t len() { return 3; }
auto dt(int a) -> decltype(a) { return a; }
int take(ns::V v) { return v.n; }
int main() {
    S s; W<ns::V> w;
    return take(s) + take(s.get()) + s.boxed().t.n + s.aliased().t + take(w.get()) + (int)len() + dt(1);
}
