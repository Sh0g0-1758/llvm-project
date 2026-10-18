template <typename T> decltype(auto) get(T& x) { return (x); }
int main() { int v = 1; return get(v); }
