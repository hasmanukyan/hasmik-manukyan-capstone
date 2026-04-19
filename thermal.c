#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <dirent.h>
#include <ctype.h>
#include <signal.h>

#define TEMP_THRESHOLD 85.0
#define COOLEST_MIN 70.0
#define NUM_CORES 4
#define CHECK_INTERVAL 1
#define MAX_CHILDREN 16

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

int get_current_core(pid_t pid) {
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

    int field = 1;
    int core = -1;
    char *ptr = buf;

    ptr = strchr(buf, '(');
    if (ptr) ptr = strchr(ptr, ')');
    if (ptr) {
        ptr++;
        field = 2;
    }

    while (ptr && field < 39) {
        if (*ptr == ' ') field++;
        ptr++;
    }

    if (ptr) {
        core = atoi(ptr);
    }

    return core;
}

void move_to_core(pid_t pid, int core) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(core, &mask);

    if (sched_setaffinity(pid, sizeof(mask), &mask) == 0) {
        printf("  -> Successfully moved PID %d to core %d\n", pid, core);
    } else {
        perror("  -> Failed to move process");
    }

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "taskset -cp %d", pid);
    system(cmd);
}

int find_coolest_core(int exclude_core, double *coolest_temp) {
    int coolest = -1;
    double min_temp = 999.0;

    for (int i = 0; i < NUM_CORES; i++) {
        if (i == exclude_core) continue;
        double temp = get_core_temp(i);
        printf("  Core %d temp: %.1f C\n", i, temp);
        if (temp >= 0 && temp < min_temp) {
            min_temp = temp;
            coolest = i;
        }
    }

    *coolest_temp = min_temp;
    printf("  Coolest core: %d (%.1f C)\n", coolest, min_temp);
    return coolest;
}

int get_child_pids(pid_t parent_pid, pid_t *children, int max_children) {
    int count = 0;
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "pgrep -P %d", parent_pid);

    FILE *fp = popen(cmd, "r");
    if (!fp) return 0;

    char line[32];
    while (fgets(line, sizeof(line), fp) && count < max_children) {
        children[count++] = atoi(line);
    }
    pclose(fp);
    return count;
}

int get_all_related_pids(pid_t pid, pid_t *pids, int max_pids) {
    int count = 0;

    pids[count++] = pid;

    pid_t children[MAX_CHILDREN];
    int num_children = get_child_pids(pid, children, MAX_CHILDREN);

    for (int i = 0; i < num_children && count < max_pids; i++) {
        pids[count++] = children[i];

        pid_t grandchildren[MAX_CHILDREN];
        int num_grand = get_child_pids(children[i], grandchildren, MAX_CHILDREN);
        for (int j = 0; j < num_grand && count < max_pids; j++) {
            pids[count++] = grandchildren[j];
        }
    }

    return count;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <check_interval> <PID>\n", argv[0]);
        printf("Example: %s 2 $(pgrep -o stress-ng)\n", argv[0]);
        return 1;
    }

    int check_interval = atoi(argv[1]);
    if (check_interval < 1) check_interval = 1;

    pid_t parent_pid = atoi(argv[2]);

    if (kill(parent_pid, 0) != 0) {
        printf("Error: Process %d does not exist.\n", parent_pid);
        return 1;
    }

    printf("Thermal scheduler started\n");
    printf("Monitoring PID: %d (and all child workers)\n", parent_pid);
    printf("Temperature threshold: %.1f C\n", TEMP_THRESHOLD);
    printf("Check interval: %d second(s)\n\n", check_interval);

    while (1) {
        if (kill(parent_pid, 0) != 0) {
            printf("Process %d no longer exists. Exiting.\n", parent_pid);
            break;
        }

        pid_t all_pids[MAX_CHILDREN * 2];
        int num_pids = get_all_related_pids(parent_pid, all_pids, MAX_CHILDREN * 2);

        printf("--- Checking %d process(es) ---\n", num_pids);

        for (int p = 0; p < num_pids; p++) {
            pid_t pid = all_pids[p];
            int current_core = get_current_core(pid);

            if (current_core < 0) {
                continue;
            }

            double temp = get_core_temp(current_core);
            printf("[PID %d] Core %d, Temp: %.1f C\n", pid, current_core, temp);

            if (temp >= TEMP_THRESHOLD) {
                printf("  WARNING: Core %d reached %.1f C - migrating PID %d!\n",
                       current_core, temp, pid);

                double coolest_temp;
                int coolest = find_coolest_core(current_core, &coolest_temp);

                if (coolest >= 0 && coolest_temp < COOLEST_MIN) {
                    move_to_core(pid, coolest);
                    sleep(1);
                } else {
                    printf("  SKIPPING: All cores too hot (coolest: %.1f C)\n", coolest_temp);
                }
            }
        }

        printf("\n");
        sleep(check_interval);
    }

    return 0;
}
