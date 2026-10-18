struct V { int n; };
struct S {
    typedef V view_type;
    operator view_type() const { return V{1}; }
};
int take(V v) { return v.n; }
int main() { S s; return take(s); }
