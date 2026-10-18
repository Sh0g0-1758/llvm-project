template <bool, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { typedef T type; };
template <bool B, class T1, class T2> struct PCC {
    template <class U1, class U2> static constexpr bool Constructible() { return true; }
};
template <class T1, class T2> struct pair {
    T1 first; T2 second;
    template <class U1 = T1, class U2 = T2,
              typename enable_if<PCC<true, T1, T2>::template Constructible<U1, U2>(), bool>::type = true>
    constexpr pair(const T1& a, const T2& b) : first(a), second(b) {}
};
int main() { int* p = nullptr; pair<int*, int*> x(p, p); return x.first == nullptr; }
