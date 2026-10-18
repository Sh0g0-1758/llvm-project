template <typename T> struct S {
    __attribute__((noinline)) void g();
};
template <typename T>
__attribute__((noinline)) void S<T>::g() {}
int main() { S<int> s; s.g(); }
