int f() { return 1; }
int k(int x) { return x; }
int outer() {
    int k(int x = f());
    return k();
}
int main() { return outer(); }
