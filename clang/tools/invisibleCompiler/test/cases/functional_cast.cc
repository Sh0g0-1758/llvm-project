template <typename T> bool f(T v) { return v < T(0); }
int main() { return f<unsigned long>(1); }
