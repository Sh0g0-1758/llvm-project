template <typename C> struct SV {
    static const long npos = -1;
    long rfind(long pos = npos) { return pos; }
    long find(long pos = 1 + 1) { return pos; }
};
int main() { SV<char> a; return a.rfind(0); }
