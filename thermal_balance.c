#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sched.h>

#define TEMP_HIGH 90.0
#define TEMP_LOW 60.0

pid_t hot_pid;
pid_t cold_pid;

void cleanup(int sig) {
    kill(hot_pid, SIGKILL);
    kill(cold_pid, SIGKILL);
    printf("\nKilling both processes before exit.\n");
    exit(0);
}

double get_core_temp(int core) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
        "sensors coretemp-isa-0000 | grep 'Core %d' | awk '{print $3}' | tr -d '+°C'",
        core);

    FILE *fp = popen(cmd, "r");
    if (!fp) return -1.0;

    double temp = -1.0;
    fscanf(fp, "%lf", &temp);
    pclose(fp);
    return temp;
}

void pin_to_core(pid_t pid, int core) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(core, &mask);
    sched_setaffinity(pid, sizeof(mask), &mask);
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage: %s <core> <hot_PID> <cold_PID>\n", argv[0]);
        printf("Example: %s 0 $(pgrep int_power) $(pgrep io_power)\n", argv[0]);
        return 1;
    }

    int core = atoi(argv[1]);
    hot_pid = atoi(argv[2]);
    cold_pid = atoi(argv[3]);

    if (kill(hot_pid, 0) != 0) {
        printf("Error: Hot process %d does not exist.\n", hot_pid);
        return 1;
    }
    if (kill(cold_pid, 0) != 0) {
        printf("Error: Cold process %d does not exist.\n", cold_pid);
        return 1;
    }

    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    pin_to_core(hot_pid, core);
    pin_to_core(cold_pid, core);

    int hot_running = 1;
    kill(cold_pid, SIGSTOP);

    printf("Thermal balance controller started\n");
    printf("Core: %d\n", core);
    printf("Hot PID: %d, Cold PID: %d\n", hot_pid, cold_pid);
    printf("Switch to cold when temp >= %.1f C\n", TEMP_HIGH);
    printf("Switch to hot when temp < %.1f C\n\n", TEMP_LOW);

    while (1) {
        int hot_exists = (kill(hot_pid, 0) == 0);
        int cold_exists = (kill(cold_pid, 0) == 0);

        if (!hot_exists && !cold_exists) {
            printf("Both processes finished. Exiting.\n");
            break;
        }

        double temp = get_core_temp(core);

        if (hot_running) {
            printf("[Core %d] Temp: %.1f C - HOT running", core, temp);
            if (temp >= TEMP_HIGH && cold_exists) {
                kill(hot_pid, SIGSTOP);
                kill(cold_pid, SIGCONT);
                hot_running = 0;
                printf(" -> SWITCH to COLD\n");
            } else {
                printf("\n");
            }
        } else {
            printf("[Core %d] Temp: %.1f C - COLD running", core, temp);
            if (temp < TEMP_LOW && hot_exists) {
                kill(cold_pid, SIGSTOP);
                kill(hot_pid, SIGCONT);
                hot_running = 1;
                printf(" -> SWITCH to HOT\n");
            } else {
                printf("\n");
            }
        }

        sleep(1);
    }

    return 0;
}
