// constant-fold replaces `idx<int>` in `in_place<idx<int>>`, whose last token `>` the lexer
// saw as the `>>` closing both lists: the replacement ends before the outer `>`.
// CHECK: in_place<2>
template <class T> constexpr int idx = sizeof(T) > 1 ? 2 : 1;
template <int N> struct in_place {};
template <int N> int get(in_place<N>) { return N; }
int main() { return get(in_place<idx<int>>{}) - 2; }
