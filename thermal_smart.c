#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sched.h>

#define TEMP_HIGH 75.0
#define TEMP_LOW 60.0
#define NUM_CORES 4

pid_t hot_pid;
pid_t cold_pid;
int current_core = 0;

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

long get_cpu_time(pid_t pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);

    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char buf[1024];
    if (!fgets(buf, sizeof(buf), fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    char *ptr = strchr(buf, ')');
    if (!ptr) return -1;
    ptr++;

    long utime = 0;
    int field = 2;
    while (ptr && field < 14) {
        if (*ptr == ' ') field++;
        ptr++;
    }
    if (ptr) utime = atol(ptr);

    return utime;
}

void detect_hot_cold(pid_t pid1, pid_t pid2) {
    printf("Detecting hot and cold tasks...\n");

    long cpu1_start = get_cpu_time(pid1);
    long cpu2_start = get_cpu_time(pid2);

    sleep(2);

    long cpu1_end = get_cpu_time(pid1);
    long cpu2_end = get_cpu_time(pid2);

    long delta1 = cpu1_end - cpu1_start;
    long delta2 = cpu2_end - cpu2_start;

    printf("PID %d CPU delta: %ld\n", pid1, delta1);
    printf("PID %d CPU delta: %ld\n", pid2, delta2);

    if (delta1 >= delta2) {
        hot_pid = pid1;
        cold_pid = pid2;
    } else {
        hot_pid = pid2;
        cold_pid = pid1;
    }

    printf("HOT task: PID %d\n", hot_pid);
    printf("COLD task: PID %d\n\n", cold_pid);
}

void pin_to_core(pid_t pid, int core) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(core, &mask);
    sched_setaffinity(pid, sizeof(mask), &mask);
}

int find_coolest_core() {
    int coolest = 0;
    double min_temp = 999.0;

    for (int i = 0; i < NUM_CORES; i++) {
        double temp = get_core_temp(i);
        if (temp >= 0 && temp < min_temp) {
            min_temp = temp;
            coolest = i;
        }
    }
    return coolest;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <PID1> <PID2>\n", argv[0]);
        return 1;
    }

    pid_t pid1 = atoi(argv[1]);
    pid_t pid2 = atoi(argv[2]);

    if (kill(pid1, 0) != 0) {
        printf("Error: Process %d does not exist.\n", pid1);
        return 1;
    }
    if (kill(pid2, 0) != 0) {
        printf("Error: Process %d does not exist.\n", pid2);
        return 1;
    }

    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    detect_hot_cold(pid1, pid2);

    current_core = find_coolest_core();
    pin_to_core(hot_pid, current_core);
    pin_to_core(cold_pid, current_core);

    int hot_running = 1;
    kill(cold_pid, SIGSTOP);

    printf("Thermal smart controller started\n");
    printf("Starting on core: %d\n", current_core);
    printf("Switch tasks when temp >= %.1f C\n", TEMP_HIGH);
    printf("Migrate to cooler core when both can't cool down\n\n");

    int cold_cycles = 0;

    while (1) {
        int hot_exists = (kill(hot_pid, 0) == 0);
        int cold_exists = (kill(cold_pid, 0) == 0);

        if (!hot_exists && !cold_exists) {
            printf("Both processes finished. Exiting.\n");
            break;
        }

        double temp = get_core_temp(current_core);

        if (hot_running) {
            printf("[Core %d] Temp: %.1f C - HOT running", current_core, temp);
            if (temp >= TEMP_HIGH && cold_exists) {
                kill(hot_pid, SIGSTOP);
                kill(cold_pid, SIGCONT);
                hot_running = 0;
                cold_cycles = 0;
                printf(" -> SWITCH to COLD\n");
            } else {
                printf("\n");
            }
        } else {
            printf("[Core %d] Temp: %.1f C - COLD running", current_core, temp);
            cold_cycles++;

            if (temp < TEMP_LOW && hot_exists) {
                kill(cold_pid, SIGSTOP);
                kill(hot_pid, SIGCONT);
                hot_running = 1;
                printf(" -> SWITCH to HOT\n");
            } else if (cold_cycles > 10 && temp >= TEMP_LOW) {
                int new_core = find_coolest_core();
                if (new_core != current_core) {
                    pin_to_core(hot_pid, new_core);
                    pin_to_core(cold_pid, new_core);
                    current_core = new_core;
                    printf(" -> MIGRATE to Core %d\n", new_core);
                    cold_cycles = 0;
                } else {
                    printf(" [waiting to cool]\n");
                }
            } else {
                printf("\n");
            }
        }

        sleep(1);
    }

    return 0;
}
