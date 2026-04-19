#include <stdio.h>

int main() {
    double result = 1.0;
    double base = 3.0;
    volatile double sink;

    for (long long i = 0; i < 2000000000LL; i++) {
        result = result * base;
        if (result > 1e15) result = 1.0;
        sink = result;
    }

    printf("Final result: %f\n", result);
    return 0;
}
