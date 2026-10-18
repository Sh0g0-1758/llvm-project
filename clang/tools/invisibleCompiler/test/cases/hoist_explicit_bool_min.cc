struct P {
    explicit operator bool() const { return true; }
};
int main() {
    P p;
    if (p) return 1;
    return 0;
}
