// hoist-calls keeps the order C++17 sequences calls in: the left operand of `,`, `<<` and `>>`
// before the right one, and the right operand of `=` before the left one.
extern "C" int printf(const char *, ...);
int trace(int v) { printf("%d ", v); return v; }
int slots[8];
int &slot(int v) { return slots[trace(v)]; }
struct Out { const Out &operator<<(int) const { return *this; } };
int main() {
  int r = (trace(1), trace(trace(2) + 1));
  slot(4) = trace(5);
  Out o;
  o << trace(6) << trace(trace(7) + 1);
  printf("\n");
  return r - 3;
}
