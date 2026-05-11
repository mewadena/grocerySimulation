/*
 * ============================================================
 *  GROCERY STORE QUEUE SIMULATION -- JSON EXPORT
 *  Course  : BIC10403 Data Structure
 *  Project : Project 4 - Grocery Store Queue Simulation
 *  Group   : Group 1
 *
 *  Members :
 *    - Nur Ameera Adriana    (CI250060)
 *    - Alya Nurjannah        (CI250082)
 *    - Muhammad Faris Hakimi (CI250125)
 *    - Muhammad Firdaus Hakimi (CI250086)
 *    - Nur Atiqa Najwa       (CI250145)
 *
 *  HOW TO COMPILE:
 *      gcc grocery_simulation_json.c -o grocery_simulation_json.exe
 *
 *  HOW TO RUN:
 *      grocery_simulation_json.exe
 *
 *  OUTPUT:
 *      Writes simulation state to state.json each tick
 *      Open index.html in browser to view dashboard
 * ============================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

#define NUM_QUEUES            5
#define MIN_SERVICE           3
#define MAX_SERVICE           12
#define MIN_PATIENCE          5
#define MAX_PATIENCE          20
#define JOCKEY_INTERVAL       5
#define ARRIVAL_CHANCE        55
#define MAX_LOG               14
#define TICK_DELAY_MS         300
#define REAL_TIME_MULTIPLIER  1
#define MAX_LINE_LENGTH       5

typedef struct Customer {
    int id;
    int arrival_time;
    int service_time;
    int wait_time;
    int patience_limit;
    struct Customer* next;
} Customer;

typedef struct Queue {
    Customer* front;
    Customer* rear;
    int size;
} Queue;

int      total_arrived   = 0;
int      total_served    = 0;
int      total_balked    = 0;
int      total_reneged   = 0;
int      total_wait      = 0;
int      customer_id     = 1;
int      MAX_QUEUE_LEN   = 5;
int      TOTAL_TIME      = 480;
HANDLE   hConsole;

void init_queue(Queue* q) {
    q->front = NULL;
    q->rear  = NULL;
    q->size  = 0;
}

void enqueue(Queue* q, Customer* c) {
    c->next = NULL;
    if (q->rear == NULL) {
        q->front = c;
        q->rear  = c;
    } else {
        q->rear->next = c;
        q->rear = c;
    }
    q->size++;
}

Customer* dequeue(Queue* q) {
    if (!q->front) return NULL;
    Customer* s = q->front;
    q->front = q->front->next;
    if (!q->front) q->rear = NULL;
    q->size--;
    return s;
}

int random_range(int min, int max) {
    return (rand() % (max - min + 1)) + min;
}

Customer* create_customer(int clock) {
    Customer* c       = (Customer*)malloc(sizeof(Customer));
    c->id             = customer_id++;
    c->arrival_time   = clock;
    c->service_time   = random_range(MIN_SERVICE, MAX_SERVICE);
    c->wait_time      = 0;
    c->patience_limit = random_range(MIN_PATIENCE, MAX_PATIENCE);
    c->next           = NULL;
    return c;
}

int find_shortest_queue(Queue queues[]) {
    int min = 0;
    for (int i = 1; i < NUM_QUEUES; i++)
        if (queues[i].size < queues[min].size) min = i;
    return min;
}

int any_queue_not_empty(Queue queues[]) {
    for (int i = 0; i < NUM_QUEUES; i++)
        if (queues[i].size > 0) return 1;
    return 0;
}

void clock_to_time(int clock, char* buf) {
    int total_min = 8 * 60 + clock;
    sprintf(buf, "%02d:%02d", total_min / 60, total_min % 60);
}

void export_to_json(Queue queues[], int clock, int store_open, FILE* log_file) {
    FILE* fp = fopen("state.json", "w");
    if (!fp) return;

    char time_str[10];
    clock_to_time(clock, time_str);

    fprintf(fp, "{\n");
    fprintf(fp, "  \"clock\": %d,\n", clock);
    fprintf(fp, "  \"totalTime\": %d,\n", TOTAL_TIME);
    fprintf(fp, "  \"storeTime\": \"%s\",\n", time_str);
    fprintf(fp, "  \"storeOpen\": %s,\n", store_open ? "true" : "false");
    fprintf(fp, "  \"stats\": {\n");
    fprintf(fp, "    \"arrived\": %d,\n", total_arrived);
    fprintf(fp, "    \"served\": %d,\n", total_served);
    fprintf(fp, "    \"balked\": %d,\n", total_balked);
    fprintf(fp, "    \"reneged\": %d,\n", total_reneged);
    fprintf(fp, "    \"avgWait\": %.1f\n", total_served > 0 ? (float)total_wait / total_served : 0.0f);
    fprintf(fp, "  },\n");

    fprintf(fp, "  \"queues\": [\n");
    for (int i = 0; i < NUM_QUEUES; i++) {
        fprintf(fp, "    {\n");
        fprintf(fp, "      \"id\": %d,\n", i + 1);
        fprintf(fp, "      \"size\": %d,\n", queues[i].size);
        fprintf(fp, "      \"maxSize\": %d,\n", MAX_QUEUE_LEN);
        fprintf(fp, "      \"customers\": [");
        
        Customer* cur = queues[i].front;
        int count = 0;
        while (cur != NULL) {
            if (count > 0) fprintf(fp, ", ");
            fprintf(fp, "{\"id\": %d, \"serviceLeft\": %d, \"waitTime\": %d, \"patience\": %d}",
                    cur->id, cur->service_time, cur->wait_time, cur->patience_limit);
            cur = cur->next;
            count++;
        }
        fprintf(fp, "]\n");
        fprintf(fp, "    }%s\n", i < NUM_QUEUES - 1 ? "," : "");
    }
    fprintf(fp, "  ],\n");

    fprintf(fp, "  \"finished\": false\n");
    fprintf(fp, "}\n");

    fclose(fp);
}

void export_log_to_json() {
    FILE* fp = fopen("log.json", "w");
    if (!fp) return;

    fprintf(fp, "{\"finished\": true}\n");
    fclose(fp);
}

void phase_arrival(Queue queues[], int clock) {
    if ((rand() % 100) >= ARRIVAL_CHANCE) return;

    total_arrived++;

    int all_full = 1;
    for (int i = 0; i < NUM_QUEUES; i++)
        if (queues[i].size < MAX_QUEUE_LEN) { all_full = 0; break; }

    if (all_full) {
        total_balked++;
        return;
    }

    int shortest = find_shortest_queue(queues);
    Customer* c = create_customer(clock);
    enqueue(&queues[shortest], c);
}

void phase_service(Queue queues[]) {
    for (int i = 0; i < NUM_QUEUES; i++) {
        if (queues[i].front) {
            queues[i].front->service_time--;
            if (queues[i].front->service_time <= 0) {
                Customer* done = dequeue(&queues[i]);
                total_wait += done->wait_time;
                total_served++;
                free(done);
            }
        }
    }
}

void phase_reneging(Queue queues[]) {
    for (int i = 0; i < NUM_QUEUES; i++) {
        if (!queues[i].front) continue;

        Customer* prev    = queues[i].front;
        Customer* current = queues[i].front->next;

        while (current != NULL) {
            current->wait_time++;

            if (current->wait_time > current->patience_limit) {
                prev->next = current->next;
                if (current == queues[i].rear) queues[i].rear = prev;

                Customer* rem = current;
                current = current->next;
                free(rem);
                queues[i].size--;
                total_reneged++;
            } else {
                prev    = current;
                current = current->next;
            }
        }
    }
}

void phase_jockeying(Queue queues[]) {
    for (int i = 0; i < NUM_QUEUES; i++) {
        if (!queues[i].front) continue;

        Customer* prev    = queues[i].front;
        Customer* current = queues[i].front->next;

        while (current != NULL) {
            int shortest = find_shortest_queue(queues);

            if (shortest != i && queues[shortest].size < queues[i].size - 1) {
                prev->next = current->next;
                if (current == queues[i].rear) queues[i].rear = prev;
                queues[i].size--;

                Customer* sw = current;
                current = current->next;
                sw->next = NULL;
                enqueue(&queues[shortest], sw);
            } else {
                prev    = current;
                current = current->next;
            }
        }
    }
}

int main() {
    srand((unsigned int)time(NULL));
    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    printf("  ================================================\n");
    printf("    GROCERY STORE QUEUE SIMULATION\n");
    printf("    BIC10403 Data Structure  |  Group 1\n");
    printf("  ================================================\n\n");

    printf("\n  Starting simulation... Open index.html in browser to view.\n");
    printf("  (Using defaults: max queue=5, hours=8)\n");
    printf("  Press Ctrl+C to stop.\n\n");

    Queue queues[NUM_QUEUES];
    for (int i = 0; i < NUM_QUEUES; i++) init_queue(&queues[i]);

    int clock = 0;
    while (clock < TOTAL_TIME) {
        export_to_json(queues, clock, 1, NULL);
        phase_arrival(queues, clock);
        phase_service(queues);
        phase_reneging(queues);
        if (clock % JOCKEY_INTERVAL == 0) phase_jockeying(queues);
        Sleep(TICK_DELAY_MS);
        clock++;
    }

    while (any_queue_not_empty(queues)) {
        export_to_json(queues, clock, 0, NULL);
        phase_service(queues);
        phase_reneging(queues);
        Sleep(TICK_DELAY_MS);
        clock++;
    }

    export_log_to_json();
    printf("\n  Simulation complete!\n");
    return 0;
}