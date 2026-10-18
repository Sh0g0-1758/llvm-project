template <typename T> void h(T);
template <typename T> struct X { template <typename U> void f(U); };
template <typename T> void h2(T);
template <typename T> struct S {
    friend void h<S<int>>(S<int>);
};
template <typename T> void h2(T) {}
int main() { S<int> s; }
