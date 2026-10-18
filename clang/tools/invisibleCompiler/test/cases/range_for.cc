namespace std { template <class E> class initializer_list { const E* b; unsigned long n; public: const E* begin() const { return b; } const E* end() const { return b + n; } }; }
namespace ns {
struct Vec { int d[3] = {1, 2, 3}; int* begin() { return d; } int* end() { return d + 3; } };
struct Bag { int d[2] = {4, 5}; };
int* begin(Bag& b) { return b.d; }
int* end(Bag& b) { return b.d + 2; }
struct It { int* p; int& operator*() const { return *p; } It& operator++() { ++p; return *this; } bool operator!=(const It& o) const { return p != o.p; } };
struct List { int d[2] = {6, 7}; It begin() { return {d}; } It end() { return {d + 2}; } };
struct Pair { int a; int b; };
}
template <typename T> int sum(T& c) { int s = 0; for (auto x : c) s += x; return s; }
int f() {
    int s = 0;
    ns::Vec v; ns::Bag bag; ns::List l;
    int arr[4] = {1, 2, 3, 4};
    ns::Pair ps[2] = {{1, 2}, {3, 4}};
    for (int x : v) s += x;
    for (const auto& x : arr) { if (x == 2) continue; s += x; }
    for (auto x : bag) s += x;
    for (int& x : l) { x += 1; s += x; }
    for (auto [a, b] : ps) s += a * b;
    for (int n = 2; auto x : arr) s += x * n;
    for (int x : {7, 8, 9}) { if (x == 9) break; s += x; }
    for (int x : v) for (int y : arr) s += x * y;
    return s + sum(v);
}
int main() { return f() == 0; }
