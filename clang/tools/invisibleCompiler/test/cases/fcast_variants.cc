struct P { int x; };
template <typename T> T brace(int v) { return T{v}; }
template <typename T> T ebrace() { return T{}; }
template <typename T> T conv(double d) { return T(d); }
template <typename T> const char* ptr(const char* p) { return T(p); }
template <typename R> bool chk(long v) {
    struct Local { static bool f(long x) { return x < R(1) || x > R(2); } };
    return Local::f(v);
}
int main() { return brace<long long>(1) + ebrace<unsigned int>() + conv<long double>(1.0) + (ptr<const char*>("a") != 0) + chk<unsigned long>(5); }
