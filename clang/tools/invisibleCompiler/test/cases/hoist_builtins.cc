#include <stddef.h>
unsigned long len(const char* s) { return __builtin_strlen(s) + 1; }
long diff(const char* a, const char* b) { return __builtin_strlen(a) - __builtin_strlen(b); }
size_t user_len(const char* s);
size_t twice(const char* s) { return user_len(s) * 2; }
int main() { return (int)len("ab") + (int)diff("a", "b") + (int)twice("x"); }
