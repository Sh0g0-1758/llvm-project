struct A {
    friend int f(A a, int x = 1) { return x; }
};
int main() { return f(A{}); }
