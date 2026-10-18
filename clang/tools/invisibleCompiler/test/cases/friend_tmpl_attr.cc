namespace ns { struct A { template <typename T> friend T h(A, T t) { return t; } }; }
int main() { ns::A a; return h(a, 1); }
