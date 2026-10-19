#include <iostream>

int add(int a, int b);   // declared but never defined

int main() {
    std::cout << add(2, 3) << "\n";
    return 0;
}
