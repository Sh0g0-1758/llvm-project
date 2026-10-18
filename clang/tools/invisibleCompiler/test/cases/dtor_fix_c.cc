template <typename T> struct A { ~A() noexcept(false) {} };
template<> A<int>::~A() noexcept(false);
int main() { A<int> a; }
template<> A<int>::~A() noexcept(false) {}
