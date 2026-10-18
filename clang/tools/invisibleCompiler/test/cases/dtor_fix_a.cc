template <typename T> struct A { ~A() {} };
template<> A<int>::~A();
int main() { A<int> a; }
template<> A<int>::~A() {}
