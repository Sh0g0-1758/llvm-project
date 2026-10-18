template <typename T> struct A { ~A() noexcept {} };
template<> A<int>::~A() noexcept;
int main() { A<int> a; }
template<> A<int>::~A() noexcept {}
