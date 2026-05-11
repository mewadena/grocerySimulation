/*
 * ============================================================
 *  GROCERY STORE QUEUE SIMULATION -- LIVE INTERFACE
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
 *      gcc grocery_simulation.c -o grocery_simulation.exe
 *
 *  HOW TO RUN:
 *      grocery_simulation.exe
 *
 *  SIMULATION LOGIC:
 *    - 1 tick = 1 simulation minute
 *    - 1 simulation minute = REAL_TIME_MULTIPLIER real minutes
 *    - Default store: 08:00 to 16:00 = 480 ticks
 *    - Balking  : customer leaves if the shortest line is already full
 *    - Reneging : customer leaves if waited beyond patience limit
 *    - Jockeying: customer switches if another line is 2+ shorter
 *
 *  TIME SCALE (fixed):
 *    - REAL_TIME_MULTIPLIER = 1  (1 sim-min = 1 real minute)
 *    - Service time  : 3 - 12 sim-min  (realistic checkout time)
 *    - Patience      : 5 - 20 sim-min  (realistic waiting tolerance)
 *    - Arrival chance: 55% per tick    (realistic store traffic)
 * ============================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

/* ============================================================
 *  SIMULATION CONSTANTS  (all in real minutes — multiplier = 1)
 * ============================================================ */
#define NUM_QUEUES            5
#define MIN_SERVICE           8      /* min checkout time : 8 real min   */
#define MAX_SERVICE           16     /* max checkout time : 16 real min  */
#define MIN_PATIENCE          15     /* min patience      : 15 real min  */
#define MAX_PATIENCE          60     /* max patience      : 60 real min  */
#define JOCKEY_INTERVAL       5      /* jockey check every 5 ticks       */
#define ARRIVAL_CHANCE        78     /* 78% chance per tick of arrival   */
#define MAX_LOG               14     /* event log lines shown            */
#define TICK_DELAY_MS         300    /* ms per tick (presentation speed) */
#define REAL_TIME_MULTIPLIER  1      /* 1 sim-min = 1 real minute        */

/* ============================================================
 *  WINDOWS CONSOLE COLOURS
 * ============================================================ */
#define CLR_WHITE        FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_BLUE
#define CLR_BRIGHT_WHITE FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_BLUE|FOREGROUND_INTENSITY
#define CLR_CYAN         FOREGROUND_GREEN|FOREGROUND_BLUE|FOREGROUND_INTENSITY
#define CLR_GREEN        FOREGROUND_GREEN|FOREGROUND_INTENSITY
#define CLR_YELLOW       FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY
#define CLR_RED          FOREGROUND_RED|FOREGROUND_INTENSITY
#define CLR_MAGENTA      FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_INTENSITY
#define CLR_BLUE         FOREGROUND_BLUE|FOREGROUND_INTENSITY
#define CLR_DARK_GREY    FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_BLUE

/* ============================================================
 *  STRUCTS
 * ============================================================ */

/*
 * Customer node — stored in the linked-list queue.
 * Fields:
 *   id             : unique customer number
 *   arrival_time   : sim-tick when customer joined the queue
 *   service_time   : ticks remaining until checkout is done
 *   wait_time      : ticks spent waiting (not being served)
 *   patience_limit : max ticks customer will wait before reneging
 *   next           : pointer to next customer in line
 */
typedef struct Customer {
    int id;
    int arrival_time;
    int service_time;
    int wait_time;
    int patience_limit;
    struct Customer* next;
} Customer;

/*
 * Queue — singly linked list with front/rear pointers and a size counter.
 * front : customer currently being served (head of list)
 * rear  : last customer in line (tail of list)
 * size  : current number of customers in this line
 */
typedef struct Queue {
    Customer* front;
    Customer* rear;
    int size;
} Queue;

/* Log entry for the on-screen event log panel */
typedef struct {
    char message[80];
    WORD colour;
} LogEntry;

/* ============================================================
 *  GLOBALS
 * ============================================================ */
int      total_arrived   = 0;
int      total_served    = 0;
int      total_balked    = 0;
int      total_reneged   = 0;
int      total_jockeyed  = 0;
int      total_wait      = 0;   /* accumulated wait time for served customers */
int      customer_id     = 1;
int      MAX_LINE_LENGTH = 5;   /* user-configurable at startup              */
int      TOTAL_TIME      = 480; /* user-configurable at startup              */
LogEntry event_log[MAX_LOG];
int      log_count       = 0;
HANDLE   hConsole;

/* ============================================================
 *  CONSOLE HELPER FUNCTIONS
 * ============================================================ */

/* Move cursor to (x=col, y=row) */
void gotoxy(int x, int y) {
    COORD c = { (SHORT)x, (SHORT)y };
    SetConsoleCursorPosition(hConsole, c);
}

void set_colour(WORD w)  { SetConsoleTextAttribute(hConsole, w); }
void reset_colour()      { SetConsoleTextAttribute(hConsole, CLR_WHITE); }

void print_at(int x, int y, WORD w, const char* t) {
    gotoxy(x, y); set_colour(w); printf("%s", t); reset_colour();
}

void clear_screen() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    DWORD count, cells;
    COORD home = {0, 0};
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    cells = csbi.dwSize.X * csbi.dwSize.Y;
    FillConsoleOutputCharacter(hConsole, ' ', cells, home, &count);
    FillConsoleOutputAttribute(hConsole, csbi.wAttributes, cells, home, &count);
    SetConsoleCursorPosition(hConsole, home);
}

void draw_hline(int x, int y, int w, WORD col) {
    gotoxy(x, y); set_colour(col);
    for (int i = 0; i < w; i++) printf("-");
    reset_colour();
}

/* ============================================================
 *  EVENT LOG
 *  Keeps the most recent MAX_LOG events. When full, oldest is
 *  dropped (shift left) and new entry goes to the last slot.
 * ============================================================ */
void add_log(const char* msg, WORD colour) {
    if (log_count < MAX_LOG) {
        strncpy(event_log[log_count].message, msg, 79);
        event_log[log_count].colour = colour;
        log_count++;
    } else {
        for (int i = 0; i < MAX_LOG - 1; i++) event_log[i] = event_log[i + 1];
        strncpy(event_log[MAX_LOG - 1].message, msg, 79);
        event_log[MAX_LOG - 1].colour = colour;
    }
}

/* ============================================================
 *  UTILITY FUNCTIONS
 * ============================================================ */

/* Returns a random integer in [min, max] inclusive */
int random_range(int min, int max) {
    return (rand() % (max - min + 1)) + min;
}

/* Convert sim-clock tick to store time string "HH:MM" (store opens 08:00) */
void clock_to_time(int clock, char* buf) {
    int total_min = 8 * 60 + clock;
    sprintf(buf, "%02d:%02d", total_min / 60, total_min % 60);
}

/* Returns 1 if at least one queue has customers */
int any_queue_not_empty(Queue queues[]) {
    for (int i = 0; i < NUM_QUEUES; i++)
        if (queues[i].size > 0) return 1;
    return 0;
}

/* ============================================================
 *  QUEUE OPERATIONS
 * ============================================================ */

/* Initialise an empty queue */
void init_queue(Queue* q) {
    q->front = NULL;
    q->rear  = NULL;
    q->size  = 0;
}

/*
 * Enqueue: append customer to the rear of queue q.
 * If queue is empty, both front and rear point to the new node.
 */
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

/*
 * Dequeue: remove and return the front customer.
 * Returns NULL if queue is empty.
 */
Customer* dequeue(Queue* q) {
    if (!q->front) return NULL;
    Customer* s = q->front;
    q->front = q->front->next;
    if (!q->front) q->rear = NULL;
    q->size--;
    return s;
}

/*
 * Create a new Customer on the heap.
 * service_time and patience_limit are randomised within the defined ranges.
 * Note: total_arrived is NOT incremented here — handled in phase_arrival()
 *       so balked customers are also counted correctly.
 */
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

/*
 * find_shortest_queue: linear scan to find the queue with fewest customers.
 * Returns the index (0-4) of the shortest queue.
 * Ties broken by lowest index (leftmost cashier).
 */
int find_shortest_queue(Queue queues[]) {
    int min = 0;
    for (int i = 1; i < NUM_QUEUES; i++)
        if (queues[i].size < queues[min].size) min = i;
    return min;
}

/* ============================================================
 *  DRAW SCREEN
 *  Full in-place redraw every tick.
 *  Layout (row by row):
 *    0-2  : title banner
 *    3    : time + store status
 *    4    : day progress bar
 *    5    : time-scale note
 *    6    : separator
 *    7    : cashier header
 *    8-12 : one row per queue (Lines 1-5)
 *    13   : separator
 *    14   : live stats
 *    15   : separator
 *    16   : "EVENT LOG:" header
 *    17-30: log entries (MAX_LOG = 14 lines)
 *    31   : separator
 *    32   : legend
 * ============================================================ */
void draw_screen(Queue queues[], int clock, int store_open) {
    char time_str[10];
    clock_to_time(clock, time_str);

    /* ---- Title ---- */
    print_at(0, 0, CLR_CYAN,
        "================================================================");
    print_at(0, 1, CLR_CYAN,
        "  GROCERY STORE QUEUE SIMULATION      BIC10403  |  Group 1     ");
    print_at(0, 2, CLR_CYAN,
        "================================================================");

    /* ---- Time & Status ---- */
    gotoxy(2, 3);
    set_colour(CLR_BRIGHT_WHITE); printf("Store Time: ");
    set_colour(CLR_YELLOW);       printf("%s", time_str);
    set_colour(CLR_BRIGHT_WHITE); printf("   Status: ");
    if (store_open) {
        set_colour(CLR_GREEN);
        printf("[ OPEN  ]  Tick %d/%d          ", clock, TOTAL_TIME);
    } else {
        set_colour(CLR_RED);
        printf("[ CLOSED ] Serving remaining...  ");
    }
    reset_colour();

    /* ---- Progress Bar ---- */
    gotoxy(2, 4);
    set_colour(CLR_DARK_GREY); printf("Progress : [");
    int prog = (TOTAL_TIME > 0) ? (clock * 42) / TOTAL_TIME : 42;
    if (prog > 42) prog = 42;
    for (int i = 0; i < 42; i++) {
        if (i < prog) { set_colour(CLR_GREEN);     printf("|"); }
        else          { set_colour(CLR_DARK_GREY); printf("."); }
    }
    set_colour(CLR_DARK_GREY);
    printf("] %3d%%", TOTAL_TIME > 0 ? (clock * 100) / TOTAL_TIME : 100);
    reset_colour();

    /* ---- Time Scale Note ---- */
    gotoxy(2, 5);
    set_colour(CLR_DARK_GREY);
    printf("(1 sim-min = 1 real min | service %d-%d min | patience %d-%d min)     ",
           MIN_SERVICE, MAX_SERVICE, MIN_PATIENCE, MAX_PATIENCE);
    reset_colour();

    draw_hline(0, 6, 64, CLR_BLUE);

    /* ---- Queue Header ---- */
    print_at(0, 7, CLR_CYAN,
        "  CASHIER LINES   [CYAN=serving now]  [WHITE=waiting]");

    /* ---- Each Queue Row ---- */
    for (int i = 0; i < NUM_QUEUES; i++) {
        int row = 8 + i;
        gotoxy(0, row);

        /* colour by fill level: green < 2/3 full, yellow >= 2/3, red = full */
        WORD lc = CLR_GREEN;
        if      (queues[i].size == MAX_LINE_LENGTH)         lc = CLR_RED;
        else if (queues[i].size >= MAX_LINE_LENGTH * 2 / 3) lc = CLR_YELLOW;

        set_colour(CLR_BRIGHT_WHITE); printf("  Line %d ", i + 1);
        set_colour(lc);               printf("[%2d/%2d] ", queues[i].size, MAX_LINE_LENGTH);

        /* fill bar */
        set_colour(CLR_DARK_GREY); printf("[");
        int filled = MAX_LINE_LENGTH > 0 ? (queues[i].size * 8) / MAX_LINE_LENGTH : 0;
        if (filled > 8) filled = 8;
        for (int b = 0; b < 8; b++) {
            if (b < filled) { set_colour(lc);          printf("|"); }
            else            { set_colour(CLR_DARK_GREY); printf("."); }
        }
        set_colour(CLR_DARK_GREY); printf("] ");

        /* customer IDs — front is cyan (being served), rest white */
        Customer* cur = queues[i].front;
        int count = 0;
        while (cur != NULL && count < 7) {
            if (count == 0) { set_colour(CLR_CYAN);  printf("[C%03d]", cur->id); }
            else            { set_colour(CLR_WHITE); printf("[C%03d]", cur->id); }
            cur = cur->next;
            count++;
        }
        if (queues[i].size > 7) { set_colour(CLR_YELLOW); printf("+%d", queues[i].size - 7); }
        if (queues[i].size == 0){ set_colour(CLR_DARK_GREY); printf("(empty)              "); }

        set_colour(CLR_WHITE); printf("         ");
        reset_colour();
    }

    draw_hline(0, 13, 64, CLR_BLUE);

    /* ---- Live Stats ---- */
    gotoxy(0, 14);
    set_colour(CLR_CYAN);   printf("  Arrived:%-4d", total_arrived);
    set_colour(CLR_GREEN);  printf("  Served:%-4d",  total_served);
    set_colour(CLR_YELLOW); printf("  Balked:%-4d",  total_balked);
    set_colour(CLR_RED);    printf("  Reneged:%-4d", total_reneged);
    set_colour(CLR_MAGENTA);printf("  Jockey:%-4d", total_jockeyed);
    if (total_served > 0) {
        set_colour(CLR_MAGENTA);
        printf("  AvgWait:%.1f min",
               (float)total_wait / total_served);
    }
    printf("          ");
    reset_colour();

    draw_hline(0, 15, 64, CLR_BLUE);

    /* ---- Event Log ---- */
    print_at(0, 16, CLR_CYAN, "  EVENT LOG:");
    for (int i = 0; i < MAX_LOG; i++) {
        gotoxy(2, 17 + i);
        if (i < log_count) {
            set_colour(event_log[i].colour);
            printf("%-62s", event_log[i].message);
        } else {
            printf("%-62s", "");
        }
        reset_colour();
    }

    draw_hline(0, 31, 64, CLR_BLUE);

    /* ---- Legend ---- */
    gotoxy(0, 32);
    set_colour(CLR_CYAN);    printf("CYAN=Serving ");
    set_colour(CLR_WHITE);   printf("WHITE=Waiting ");
    set_colour(CLR_GREEN);   printf("GREEN=Served ");
    set_colour(CLR_YELLOW);  printf("YELLOW=Balked ");
    set_colour(CLR_RED);     printf("RED=Reneged ");
    set_colour(CLR_MAGENTA); printf("PINK=Jockeyed");
    reset_colour();
}

/* ============================================================
 *  SIMULATION PHASES
 * ============================================================ */

/*
 * PHASE 1: ARRIVAL
 *
 * Each tick there is an ARRIVAL_CHANCE% probability that a new customer
 * arrives. The customer first checks the shortest line:
 *
 *   BALKING (matches proposal pseudocode):
 *     If the shortest line is already at MAX_LINE_LENGTH, the customer
 *     balks — they see no line with room and leave immediately.
 *     This is stricter than "all lines full" and matches the pseudocode:
 *       IF queues[shortest].size >= MAX_LINE_LENGTH THEN balk
 *
 *   If there is room, the customer joins the shortest line.
 *
 * total_arrived is incremented for BOTH arriving and balking customers
 * because the customer did arrive at the store — they just didn't join.
 */
void phase_arrival(Queue queues[], int clock) {
    if ((rand() % 100) >= ARRIVAL_CHANCE) return;   /* no arrival this tick */

    total_arrived++;
    char buf[80];

    /* BALKING: customer checks all 5 lines.
     * Only leaves if EVERY line is at capacity — if even one line has
     * room, a real customer would join it. This matches real grocery
     * store behaviour and the spirit of Project 4's description. */
    int all_full = 1;
    for (int i = 0; i < NUM_QUEUES; i++)
        if (queues[i].size < MAX_LINE_LENGTH) { all_full = 0; break; }

    if (all_full) {
        total_balked++;
        sprintf(buf, "C#%03d BALKED   -- all 5 lines full! Left the store.",
                customer_id);
        add_log(buf, CLR_YELLOW);
        customer_id++;
        return;
    }

    int shortest = find_shortest_queue(queues);

    /* Customer joins the shortest line */
    Customer* c = create_customer(clock);
    enqueue(&queues[shortest], c);
    sprintf(buf, "C#%03d arrived  -> joined Line %d  [%d/%d]",
            c->id, shortest + 1, queues[shortest].size, MAX_LINE_LENGTH);
    add_log(buf, CLR_WHITE);
}

/*
 * PHASE 2: SERVICE
 *
 * The front customer of each non-empty queue is being served.
 * Their service_time decrements by 1 per tick.
 * When service_time reaches 0, they are dequeued and freed.
 *
 * Wait time tracking:
 *   wait_time for the front customer is NOT incremented here — it was
 *   already accumulated while they were in the waiting positions.
 *   Once they reach the front, they are being served so we stop counting.
 *   total_wait adds their accumulated wait before reaching front.
 */
void phase_service(Queue queues[]) {
    char buf[80];
    for (int i = 0; i < NUM_QUEUES; i++) {
        if (queues[i].front) {
            queues[i].front->service_time--;
            if (queues[i].front->service_time <= 0) {
                Customer* done = dequeue(&queues[i]);
                total_wait += done->wait_time;
                total_served++;
                sprintf(buf,
                    "C#%03d SERVED   -- Line %d done! (waited %d min in queue)",
                    done->id, i + 1, done->wait_time);
                add_log(buf, CLR_GREEN);
                free(done);
            }
        }
    }
}

/*
 * PHASE 3: RENEGING
 *
 * Every tick, customers who are WAITING (not the front/being-served one)
 * have their wait_time incremented.
 * If wait_time exceeds patience_limit, the customer reneges (leaves).
 *
 * Implementation uses a prev pointer to splice out reneging nodes
 * from the linked list without breaking the chain.
 *
 * The front customer is excluded because they are already being served —
 * a customer being served would not abandon their checkout.
 */
void phase_reneging(Queue queues[]) {
    char buf[80];
    for (int i = 0; i < NUM_QUEUES; i++) {
        if (!queues[i].front) continue;

        Customer* prev    = queues[i].front;
        Customer* current = queues[i].front->next;   /* start from 2nd customer */

        while (current != NULL) {
            current->wait_time++;

            if (current->wait_time > current->patience_limit) {
                /* Customer reneges — splice out of linked list */
                sprintf(buf,
                    "C#%03d RENEGED  -- Line %d! Waited %d min (limit %d min)",
                    current->id, i + 1,
                    current->wait_time,
                    current->patience_limit);
                add_log(buf, CLR_RED);

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

/*
 * PHASE 4: JOCKEYING
 *
 * Every JOCKEY_INTERVAL ticks, each waiting customer (not front) checks
 * whether another line is at least 2 positions shorter than their current line.
 * If so, they switch to that shorter line.
 *
 * find_shortest_queue() is called fresh per customer so that multiple
 * customers jockeying in the same tick don't all pile into one line.
 * The condition  queues[shortest].size < pos  ensures the destination
 * is actually shorter than the customer's current position.
 */
void phase_jockeying(Queue queues[]) {
    char buf[80];
    for (int i = 0; i < NUM_QUEUES; i++) {
        if (!queues[i].front) continue;

        Customer* prev    = queues[i].front;
        Customer* current = queues[i].front->next;
        int pos = 1;

        while (current != NULL) {
            int shortest = find_shortest_queue(queues);

            if (shortest != i && queues[shortest].size < pos) {
                sprintf(buf,
                    "C#%03d JOCKEYED -- Line %d -> Line %d (pos %d -> %d)",
                    current->id, i + 1, shortest + 1,
                    pos, queues[shortest].size);
                add_log(buf, CLR_MAGENTA);

                Customer* next = current->next;
                prev->next = next;
                if (current == queues[i].rear) queues[i].rear = prev;
                queues[i].size--;

                current->next = NULL;
                enqueue(&queues[shortest], current);
                total_jockeyed++;
                current = next;
            } else {
                prev    = current;
                current = current->next;
                pos++;
            }
        }
    }
}

/* ============================================================
 *  FINAL REPORT
 *  Shown after all queues are empty post-closing.
 * ============================================================ */
void show_final_report() {
    clear_screen();
    print_at(8,  1, CLR_CYAN,         "==================================================");
    print_at(8,  2, CLR_CYAN,         "           SIMULATION COMPLETE                    ");
    print_at(8,  3, CLR_CYAN,         "           FINAL STATISTICS REPORT                ");
    print_at(8,  4, CLR_CYAN,         "==================================================");
    print_at(8,  5, CLR_BRIGHT_WHITE, "  BIC10403 Data Structure  |  Group 1             ");
    print_at(8,  6, CLR_BRIGHT_WHITE, "  Project 4: Grocery Store Queue Simulation        ");
    print_at(8,  7, CLR_CYAN,         "--------------------------------------------------");

    gotoxy(8, 8); set_colour(CLR_WHITE);
    printf("  Store Hours         : 08:00 - %02d:%02d  (%d sim-min)",
           (8 * 60 + TOTAL_TIME) / 60,
           (8 * 60 + TOTAL_TIME) % 60,
           TOTAL_TIME);

    gotoxy(8, 9); set_colour(CLR_DARK_GREY);
    printf("  Time Scale          : 1 sim-min = 1 real minute");

    gotoxy(8, 10); set_colour(CLR_DARK_GREY);
    printf("  Max Queue Length    : %d customers per line", MAX_LINE_LENGTH);

    print_at(8, 11, CLR_CYAN, "--------------------------------------------------");

    char buf[80];

    gotoxy(8, 12); set_colour(CLR_CYAN);
    sprintf(buf, "  Total Arrived       : %d customers", total_arrived);
    printf("%s", buf);

    gotoxy(8, 13); set_colour(CLR_GREEN);
    sprintf(buf, "  Total Served        : %d  (%.1f%%)",
            total_served,
            total_arrived > 0 ? (float)total_served * 100 / total_arrived : 0.0f);
    printf("%s", buf);

    gotoxy(8, 14); set_colour(CLR_YELLOW);
    sprintf(buf, "  Total Balked        : %d  (%.1f%%)",
            total_balked,
            total_arrived > 0 ? (float)total_balked * 100 / total_arrived : 0.0f);
    printf("%s", buf);

    gotoxy(8, 15); set_colour(CLR_RED);
    sprintf(buf, "  Total Reneged       : %d  (%.1f%%)",
            total_reneged,
            total_arrived > 0 ? (float)total_reneged * 100 / total_arrived : 0.0f);
    printf("%s", buf);

    gotoxy(8, 16); set_colour(CLR_MAGENTA);
    sprintf(buf, "  Total Jockeyed      : %d  (%.1f%%)",
            total_jockeyed,
            total_arrived > 0 ? (float)total_jockeyed * 100 / total_arrived : 0.0f);
    printf("%s", buf);

    print_at(8, 17, CLR_CYAN, "--------------------------------------------------");

    if (total_served > 0) {
        float avg_wait = (float)total_wait / total_served;
        gotoxy(8, 18); set_colour(CLR_MAGENTA);
        printf("  Avg Wait Time        : %.1f minutes in queue", avg_wait);

        gotoxy(8, 19); set_colour(CLR_DARK_GREY);
        printf("  (Only counts time waiting, not time being served)");
    } else {
        gotoxy(8, 18); set_colour(CLR_DARK_GREY);
        printf("  No customers were served.");
    }

    print_at(8, 20, CLR_CYAN, "--------------------------------------------------");

    /* Efficiency summary */
    gotoxy(8, 21); set_colour(CLR_BRIGHT_WHITE);
    int lost = total_balked + total_reneged;
    printf("  Customers lost       : %d  (balked + reneged)", lost);

    gotoxy(8, 22); set_colour(CLR_BRIGHT_WHITE);
    printf("  Efficiency           : %.1f%%  (served / arrived)",
           total_arrived > 0 ? (float)total_served * 100 / total_arrived : 0.0f);

    print_at(8, 23, CLR_CYAN, "--------------------------------------------------");
    print_at(8, 24, CLR_WHITE, "  Press ENTER to exit...");
    reset_colour();
    gotoxy(0, 25);
    getchar();
}

/* ============================================================
 *  MAIN
 * ============================================================ */
int main() {
    srand((unsigned int)time(NULL));
    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    /* Hide blinking cursor for cleaner display */
    CONSOLE_CURSOR_INFO ci;
    GetConsoleCursorInfo(hConsole, &ci);
    ci.bVisible = FALSE;
    SetConsoleCursorInfo(hConsole, &ci);
    SetConsoleTitleA("Grocery Store Queue Simulation - BIC10403 Group 1");

    /* ---- Intro / Config Screen ---- */
    clear_screen();
    set_colour(CLR_CYAN);
    printf("  ================================================\n");
    printf("    GROCERY STORE QUEUE SIMULATION\n");
    printf("    BIC10403 Data Structure  |  Group 1\n");
    printf("  ================================================\n\n");
    reset_colour();

    set_colour(CLR_BRIGHT_WHITE);
    printf("  Members:\n");
    set_colour(CLR_DARK_GREY);
    printf("    1. Nur Ameera Adriana       (CI250060)\n");
    printf("    2. Alya Nurjannah           (CI250082)\n");
    printf("    3. Muhammad Faris Hakimi    (CI250125)\n");
    printf("    4. Muhammad Firdaus Hakimi  (CI250086)\n");
    printf("    5. Nur Atiqa Najwa          (CI250145)\n\n");
    reset_colour();

    set_colour(CLR_BRIGHT_WHITE); printf("  Simulation Parameters:\n");
    set_colour(CLR_DARK_GREY);
    printf("    - 1 sim-minute  = 1 real minute\n");
    printf("    - Service time  : %d - %d minutes per customer\n", MIN_SERVICE, MAX_SERVICE);
    printf("    - Patience      : %d - %d minutes before reneging\n", MIN_PATIENCE, MAX_PATIENCE);
    printf("    - Arrival rate  : %d%% chance per minute\n", ARRIVAL_CHANCE);
    printf("    - Balking       : customer leaves if shortest line is full\n");
    printf("    - Reneging      : customer leaves if wait exceeds patience\n");
    printf("    - Jockeying     : customer switches if another line 2+ shorter\n\n");
    reset_colour();

    /* User configuration */
    printf("  Enter max queue length per line (recommended: 5) : ");
    scanf("%d", &MAX_LINE_LENGTH);
    if (MAX_LINE_LENGTH < 1) MAX_LINE_LENGTH = 1;

    int store_hours;
    printf("  Enter store operating hours    (recommended: 8)  : ");
    scanf("%d", &store_hours);
    if (store_hours < 1) store_hours = 1;
    TOTAL_TIME = store_hours * 60;

    printf("\n  Press ENTER to start simulation...");
    getchar();  /* consume leftover newline from scanf */
    getchar();  /* wait for actual ENTER press         */

    /* ---- Initialise all 5 queues ---- */
    Queue queues[NUM_QUEUES];
    for (int i = 0; i < NUM_QUEUES; i++) init_queue(&queues[i]);

    /* ============================================================
     *  MAIN SIMULATION LOOP — STORE OPEN
     *  Runs for TOTAL_TIME ticks (1 tick = 1 sim-minute).
     *  Each tick runs 4 phases in order:
     *    1. Arrival   — new customers arrive / balk
     *    2. Service   — front customer's timer decrements
     *    3. Reneging  — waiting customers may lose patience
     *    4. Jockeying — every JOCKEY_INTERVAL ticks, customers may switch
     * ============================================================ */
    int clock = 0;
    while (clock < TOTAL_TIME) {
        draw_screen(queues, clock, 1);
        phase_arrival(queues, clock);
        phase_service(queues);
        phase_reneging(queues);
        if (clock % JOCKEY_INTERVAL == 0) phase_jockeying(queues);
        Sleep(TICK_DELAY_MS);
        clock++;
    }

    /* ============================================================
     *  POST-CLOSING LOOP — STORE CLOSED, SERVE REMAINING CUSTOMERS
     *  No new arrivals. Continue service and reneging until all
     *  queues are empty.
     * ============================================================ */
    add_log("=== STORE CLOSED === Still serving remaining customers ===", CLR_RED);
    while (any_queue_not_empty(queues)) {
        draw_screen(queues, clock, 0);
        phase_service(queues);
        phase_reneging(queues);
        Sleep(TICK_DELAY_MS);
        clock++;
    }

    /* ---- Show Final Report ---- */
    show_final_report();

    /* Restore cursor visibility before exit */
    ci.bVisible = TRUE;
    SetConsoleCursorInfo(hConsole, &ci);
    return 0;
}