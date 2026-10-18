template <typename C> struct W {
    template <typename T> operator T() const { return T(1); }
};
int main() { W<char> w; int i = w; double d = w; return i + (int)d; }
