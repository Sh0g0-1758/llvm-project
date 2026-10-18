struct It {
    typedef It iterator;
    int p = 0;
    iterator& operator+=(long n) { p += n; return *this; }
    friend iterator operator+(const iterator& x, long n) {
        iterator tmp = x;
        tmp += n;
        return tmp;
    }
};
int main() { It a; return (a + 2).p; }
