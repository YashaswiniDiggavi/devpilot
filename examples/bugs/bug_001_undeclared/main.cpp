#include <iostream>

int main() {
    int total = add(2, 3);   // BUG: add() is never declared
    std::cout << total << "\n";
    return 0;
}