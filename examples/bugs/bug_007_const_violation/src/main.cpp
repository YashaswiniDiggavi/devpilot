#include <iostream>

void bump(const int& v) {
    v++;
}

int main() {
    int n = 1;
    bump(n);
    std::cout << n << "\n";
    return 0;
}
