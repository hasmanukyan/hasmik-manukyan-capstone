#include <stdio.h>

int main() {
    FILE *fp;
    char buffer[64] = "test data for io bound measurement\n";
    volatile char sink[64];

    while (1) {
        fp = fopen("/tmp/io_test.tmp", "w");
        if (fp) {
            fwrite(buffer, 1, sizeof(buffer), fp);
            fclose(fp);
        }

        fp = fopen("/tmp/io_test.tmp", "r");
        if (fp) {
            fread((char*)sink, 1, sizeof(sink), fp);
            fclose(fp);
        }
    }

    return 0;
}
