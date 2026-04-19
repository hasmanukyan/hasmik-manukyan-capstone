#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#define TEMP_HIGH 90.0
#define TEMP_LOW 60.0
#define CHECK_INTERVAL 1

pid_t monitored_pid;
int is_paused = 0;

void cleanup(int sig) {
    kill(monitored_pid, SIGKILL);
    printf("\nKilling process %d before exit.\n", monitored_pid);
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

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <check_interval> <PID>\n", argv[0]);
        return 1;
    }

    int check_interval = atoi(argv[1]);
    if (check_interval < 1) check_interval = 1;

    pid_t pid = atoi(argv[2]);
    monitored_pid = pid;

    if (kill(pid, 0) != 0) {
        printf("Error: Process %d does not exist.\n", pid);
        return 1;
    }

    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    printf("Thermal pause controller started\n");
    printf("Monitoring PID: %d\n", pid);
    printf("Pause when temp >= %.1f C\n", TEMP_HIGH);
    printf("Resume when temp < %.1f C\n\n", TEMP_LOW);

    while (1) {
        if (kill(pid, 0) != 0) {
            printf("Process %d no longer exists. Exiting.\n", pid);
            break;
        }

        int core = get_current_core(pid);
        if (core < 0) {
            sleep(check_interval);
            continue;
        }

        double temp = get_core_temp(core);
        printf("[PID %d] Core %d, Temp: %.1f C", pid, core, temp);

        if (!is_paused && temp >= TEMP_HIGH) {
            kill(pid, SIGSTOP);
            is_paused = 1;
            printf(" -> PAUSED (too hot)\n");
        } else if (is_paused && temp < TEMP_LOW) {
            kill(pid, SIGCONT);
            is_paused = 0;
            printf(" -> RESUMED (cooled down)\n");
        } else if (is_paused) {
            printf(" [PAUSED]\n");
        } else {
            printf(" [RUNNING]\n");
        }

        sleep(check_interval);
    }

    return 0;
}
