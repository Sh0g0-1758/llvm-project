namespace ns {
template <typename C, typename T> struct num_get {
    __attribute__((abi_tag("cxx11"))) int extract(int x) const;
    [[nodiscard]] int other(int x) const;
    template <typename U> int tmpl(U u) const;
};
template <typename C, typename T>
__attribute__((abi_tag("cxx11"))) int num_get<C, T>::extract(int x) const { return x; }
template <typename C, typename T>
[[nodiscard]] int num_get<C, T>::other(int x) const { return x + 1; }
template <typename C, typename T> template <typename U>
__attribute__((noinline)) int num_get<C, T>::tmpl(U u) const { return (int)u; }
}
int main() { ns::num_get<char, int> n; return n.extract(1) + n.other(2) + n.tmpl(3.0); }
