struct S {
    static const unsigned long npos = static_cast<unsigned long>(-1);
    unsigned long rfind(char c, unsigned long pos = npos) const;
};
unsigned long S::rfind(char c, unsigned long pos) const { return pos; }
int main() { return S{}.rfind('a', 0) == 0; }
