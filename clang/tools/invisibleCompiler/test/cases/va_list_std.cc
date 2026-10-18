#include <cstdio>
#include <cstdarg>
#include <string>
template <typename S, typename C>
S to_xstring(int (*conv)(C*, std::size_t, const C*, va_list), std::size_t n, const C* fmt, ...) {
    C buf[64];
    va_list args;
    va_start(args, fmt);
    const int len = conv(buf, n, fmt, args);
    va_end(args);
    return S(buf, buf + len);
}
std::string show(double d) { return to_xstring<std::string>(&std::vsnprintf, 64, "%f", d); }
int main() { return show(1.5).size() > 0 ? 0 : 1; }
