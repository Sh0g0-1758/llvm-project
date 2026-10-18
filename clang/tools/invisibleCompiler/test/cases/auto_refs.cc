int g_v = 1;
template <typename T> const auto& cref(T) { return g_v; }
template <typename T> auto* ptr(T) { return &g_v; }
template <typename T> auto& ref(T) { return g_v; }
int main() { return cref(1) + *ptr(1) + ref(1); }
