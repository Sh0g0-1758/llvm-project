template <typename C> struct SV {
    static const unsigned long npos = static_cast<unsigned long>(-1);
    unsigned long rfind(C c, unsigned long pos = npos) const { return pos; }
    unsigned long find(C c, unsigned long pos = 1 + 1) const { return pos; }
};
int main() { SV<char> a; SV<wchar_t> b; return a.rfind('x', 0) + b.find(L'y', 0); }
