struct P {
    void* p;
    explicit operator bool() const { return p != 0; }
};
bool f(const P& x) { if (x) return true; return !x && x ? false : (bool)x; }
int main() { P p{0}; return f(p); }
