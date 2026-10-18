namespace n { struct Str { Str(const char*, const char*) {} }; }
template <typename S> S make(const char* a) { return S(a, a + 1); }
int main() { make<n::Str>("x"); }
