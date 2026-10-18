struct Thrower { ~Thrower() noexcept(false) {} };
template <typename T> struct Impl { ~Impl() {} };
template <typename T> struct Expl { ~Expl() noexcept {} };
template <typename T> struct ExplFalse { ~ExplFalse() noexcept(false) {} };
template <typename T> struct ImplFalse { Thrower t; ~ImplFalse() {} };
template <typename T> struct OutOfLine { ~OutOfLine(); };
template <typename T> OutOfLine<T>::~OutOfLine() {}
template <typename T> struct Outer { struct Inner { ~Inner() {} }; };
struct Plain { ~Plain() {} };
int main() {
    Impl<int> a; Expl<int> b; ExplFalse<int> c; ImplFalse<int> d;
    OutOfLine<int> e; Outer<int>::Inner f; Plain g;
}
