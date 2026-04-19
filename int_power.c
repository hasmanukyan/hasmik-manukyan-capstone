#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    int sleep_time = atoi(argv[1]);
    long long result = 1;
    long long base = 3;
    long long i = 0;
    volatile long long sink;

    while (1) {
        result = result * base;
        if (result > 1000000000000000LL) result = 1;
        sink = result;

        if (++i % 10000000 == 0) {
            usleep(sleep_time);
        }
    }

    return 0;
}
