// A specialization is declared without its template arguments when they are deduced: written,
// they are substituted into every template of its name, which can fail hard, as libstdc++'s
// __niter_base of a reverse_iterator does for ostream_iterator, where deduction doesn't.
// CHECK-NORMAL: template<> int get\(int
template <class T> struct wrap { using type = typename T::value; };
template <class T> T get(T t) { return t; }
template <class T> typename wrap<T>::type get(wrap<T> w);
int main() { return get(3) - 3; }
