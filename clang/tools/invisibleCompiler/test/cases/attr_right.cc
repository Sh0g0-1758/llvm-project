template <typename T> T k(T t) __attribute__((noinline));
template <typename T> T k(T t) { return t; }
int main() { return k(1); }
