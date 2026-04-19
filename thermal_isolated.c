#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sched.h>

#define TEMP_HIGH 90.0
#define TEMP_LOW 75.0
#define NUM_CORES 4

int ISOLATED_CORES[2] = {2, 3};

typedef struct {
    pid_t pid;
    long cpu_delta;
    int is_hot;
} Task;

typedef struct {
    int core_id;
    pid_t hot_pid;
    pid_t cold_pid;
    int hot_running;
} CorePair;

Task tasks[4];
CorePair cores[2];
int num_tasks = 0;

void cleanup(int sig) {
    printf("\nKilling all processes...\n");
    for (int i = 0; i < num_tasks; i++) {
        kill(tasks[i].pid, SIGKILL);
    }
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

void pin_to_core(pid_t pid, int core) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(core, &mask);
    sched_setaffinity(pid, sizeof(mask), &mask);
}

void detect_hot_cold() {
    printf("Detecting hot and cold tasks...\n");

    long cpu_start[4];
    for (int i = 0; i < num_tasks; i++) {
        cpu_start[i] = get_cpu_time(tasks[i].pid);
    }

    sleep(2);

    for (int i = 0; i < num_tasks; i++) {
        long cpu_end = get_cpu_time(tasks[i].pid);
        tasks[i].cpu_delta = cpu_end - cpu_start[i];
        printf("  PID %d: CPU delta = %ld\n", tasks[i].pid, tasks[i].cpu_delta);
    }

    for (int i = 0; i < num_tasks - 1; i++) {
        for (int j = i + 1; j < num_tasks; j++) {
            if (tasks[j].cpu_delta > tasks[i].cpu_delta) {
                Task temp = tasks[i];
                tasks[i] = tasks[j];
                tasks[j] = temp;
            }
        }
    }

    int half = num_tasks / 2;
    for (int i = 0; i < num_tasks; i++) {
        tasks[i].is_hot = (i < half);
        printf("  PID %d: %s\n", tasks[i].pid, tasks[i].is_hot ? "HOT" : "COLD");
    }
    printf("\n");
}

void create_pairs() {
    pid_t hot_pids[2], cold_pids[2];
    int num_hot = 0, num_cold = 0;

    for (int i = 0; i < num_tasks; i++) {
        if (tasks[i].is_hot && num_hot < 2) {
            hot_pids[num_hot++] = tasks[i].pid;
        } else if (!tasks[i].is_hot && num_cold < 2) {
            cold_pids[num_cold++] = tasks[i].pid;
        }
    }

    printf("Creating 2 pairs on isolated cores %d and %d:\n",
           ISOLATED_CORES[0], ISOLATED_CORES[1]);

    for (int i = 0; i < 2; i++) {
        cores[i].core_id = ISOLATED_CORES[i];
        cores[i].hot_pid = hot_pids[i];
        cores[i].cold_pid = cold_pids[i];
        cores[i].hot_running = 1;

        pin_to_core(cores[i].hot_pid, ISOLATED_CORES[i]);
        pin_to_core(cores[i].cold_pid, ISOLATED_CORES[i]);
        kill(cores[i].cold_pid, SIGSTOP);

        printf("  Core %d: HOT=%d, COLD=%d\n",
               ISOLATED_CORES[i], cores[i].hot_pid, cores[i].cold_pid);
    }
    printf("\n");
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        printf("Usage: %s <PID1> <PID2> <PID3> <PID4>\n", argv[0]);
        return 1;
    }

    num_tasks = 4;
    for (int i = 0; i < 4; i++) {
        tasks[i].pid = atoi(argv[i + 1]);
        if (kill(tasks[i].pid, 0) != 0) {
            printf("Error: Process %d does not exist.\n", tasks[i].pid);
            return 1;
        }
    }

    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    printf("==========================================\n");
    printf("   Thermal Controller (Isolated Cores)\n");
    printf("==========================================\n");
    printf("Isolated cores: %d, %d\n", ISOLATED_CORES[0], ISOLATED_CORES[1]);
    printf("Managing 4 tasks (2 HOT, 2 COLD)\n\n");

    detect_hot_cold();
    create_pairs();

    printf("Starting thermal management...\n");
    printf("Switch to COLD when temp >= %.1f C\n", TEMP_HIGH);
    printf("Switch to HOT when temp < %.1f C\n\n", TEMP_LOW);

    while (1) {
        int all_done = 1;

        printf("+------+--------+---------+-------------------+\n");
        printf("| Core |  Temp  | Running | Action            |\n");
        printf("+------+--------+---------+-------------------+\n");

        for (int i = 0; i < 2; i++) {
            int hot_exists = (kill(cores[i].hot_pid, 0) == 0);
            int cold_exists = (kill(cores[i].cold_pid, 0) == 0);

            if (!hot_exists && !cold_exists) {
                printf("|  %d   |  ---   |  done   |                   |\n", cores[i].core_id);
                continue;
            }
            all_done = 0;

            double temp = get_core_temp(cores[i].core_id);
            char action[32] = "";

            if (cores[i].hot_running) {
                if (temp >= TEMP_HIGH && cold_exists) {
                    kill(cores[i].hot_pid, SIGSTOP);
                    kill(cores[i].cold_pid, SIGCONT);
                    cores[i].hot_running = 0;
                    sprintf(action, "SWITCH -> COLD");
                }
                printf("|  %d   | %5.1fC | HOT     | %-17s |\n",
                       cores[i].core_id, temp, action);
            } else {
                if (temp < TEMP_LOW && hot_exists) {
                    kill(cores[i].cold_pid, SIGSTOP);
                    kill(cores[i].hot_pid, SIGCONT);
                    cores[i].hot_running = 1;
                    sprintf(action, "SWITCH -> HOT");
                }
                printf("|  %d   | %5.1fC | COLD    | %-17s |\n",
                       cores[i].core_id, temp, action);
            }
        }
        printf("+------+--------+---------+-------------------+\n\n");

        if (all_done) {
            printf("All processes finished. Exiting.\n");
            break;
        }

        sleep(1);
    }

    return 0;
}
