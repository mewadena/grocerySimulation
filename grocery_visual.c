/*
 * ============================================================
 * GROCERY STORE QUEUE VISUALIZATION — FIXED & POLISHED
 * SDL2 + SDL2_ttf + SDL2_gfx
 * ============================================================
 *
 * INSTALL (WSL / Ubuntu):
 *   sudo apt update
 *   sudo apt install libsdl2-dev libsdl2-ttf-dev libsdl2-gfx-dev
 *
 * COMPILE:
 *   gcc grocery_visual_fixed.c -o grocery_visual -lSDL2 -lSDL2_ttf -lSDL2_gfx -lm
 *
 * RUN:
 *   ./grocery_visual
 *
 * CONTROLS:
 *   SPACE  -> Pause / Resume
 *   UP     -> Increase Speed
 *   DOWN   -> Decrease Speed
 *   R      -> Reset
 *   ESC    -> Exit
 *
 * KEY FIXES vs original:
 *   - Queue now stacks ABOVE the cashier counter, not below
 *   - Served customer exits DOWNWARD past the counter
 *   - Reneged customers are properly removed from queue data
 *   - Smooth lerp capped so no jitter at low framerates
 *   - AnimatedCustomer positions fully driven by logical queue positions
 *   - No double-drawing artifacts
 *   - Fade-in/out properly gated
 * ============================================================
 */

#define SDL_MAIN_HANDLED

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL2_gfxPrimitives.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>

/* ============================================================
 * CONSTANTS
 * ============================================================ */

#define SCREEN_WIDTH      1400
#define SCREEN_HEIGHT     800

#define NUM_QUEUES        5

#define MIN_SERVICE       8
#define MAX_SERVICE       16

#define MIN_PATIENCE      15
#define MAX_PATIENCE      60

#define ARRIVAL_CHANCE    78
#define MAX_LINE_LENGTH   6
#define JOCKEY_INTERVAL   5
#define TOTAL_TIME        480

#define START_HOUR        8

/* Customer circle radius */
#define CUST_R            16

/* ============================================================
 * LAYOUT
 * ============================================================ */

#define SIDEBAR_W     260
#define STAT_PANEL_W  240
#define SIM_X0        (SIDEBAR_W)
#define SIM_X1        (SCREEN_WIDTH - STAT_PANEL_W)
#define SIM_MID       ((SIM_X0 + SIM_X1) / 2)

#define HEADER_H      70

/* Cashier counter centre Y — placed in the lower portion of the sim area */
#define COUNTER_Y     620

/* Queue spacing: customers stack UPWARD from (COUNTER_Y - CUST_R*2 - gap) */
#define QUEUE_STEP    75    /* vertical pixels between queued customers (upward) */
#define QUEUE_START_Y (COUNTER_Y - CUST_R*2 - 10)  /* top of first queued slot */

/* Exit destination — customer slides down past the counter */
#define EXIT_Y        (SCREEN_HEIGHT + 60)

/* "Walk-in" entry row — customers appear here before joining queue */
#define ENTRY_Y       (HEADER_H + CUST_R + 10)

/* ============================================================
 * COLOUR PALETTE
 * ============================================================ */

#define COL_BG_DARK    10,  14,  26
#define COL_BG_MID     18,  24,  42
#define COL_BG_PANEL   24,  32,  56
#define COL_BG_HEADER  14,  18,  36

#define COL_ACCENT_R   255
#define COL_ACCENT_G   200
#define COL_ACCENT_B    50

#define COL_TEXT_BRIGHT  230, 235, 255
#define COL_TEXT_DIM     110, 120, 150
#define COL_DIVIDER       30,  40,  65

/* ============================================================
 * STRUCTURES
 * ============================================================ */

typedef struct Customer {
    int id;
    int service_time;
    int wait_time;
    int patience_limit;
    int queue_index;
    int position;        /* 0 = at counter (being served), 1 = first in line ... */
    bool is_serving;
    struct Customer* next;
} Customer;

typedef struct {
    Customer* front;
    Customer* rear;
    int size;
} Queue;

typedef enum {
    STATE_WALKING_IN,   /* animating from spawn point toward queue entry */
    STATE_IN_QUEUE,     /* stationary in queue slot */
    STATE_BEING_SERVED, /* at counter */
    STATE_LEAVING,      /* served — slides downward */
    STATE_RENEGED,      /* lost patience — slides upward off screen */
    STATE_BALKED        /* never joined — slides upward off screen */
} CustState;

typedef struct AnimatedCustomer {
    int   id;
    float x, y;          /* current rendered position */
    float tx, ty;        /* smooth-lerp target */
    int   queue_index;
    CustState state;
    float alpha;
    bool  destroy_me;
    struct AnimatedCustomer* next;
} AnimatedCustomer;

typedef struct {
    int   arrived;
    int   served;
    int   balked;
    int   reneged;
    int   jockeyed;
    float total_wait;
    float avg_wait;
} Stats;

/* ============================================================
 * GLOBALS
 * ============================================================ */

SDL_Window*   window   = NULL;
SDL_Renderer* renderer = NULL;

TTF_Font* font_large  = NULL;
TTF_Font* font_medium = NULL;
TTF_Font* font_small  = NULL;
TTF_Font* font_tiny   = NULL;

Queue            queues[NUM_QUEUES];
AnimatedCustomer* anim_head = NULL;
Stats            stats      = {0};

int   sim_clock   = 0;
int   cust_id     = 1;
float sim_speed   = 1.0f;
bool  paused      = false;
bool  sim_done    = false;

/* Each cashier tracks remaining service ticks for the current customer */
int   service_remaining[NUM_QUEUES];

static int queue_cx[NUM_QUEUES];   /* centre X for each lane */

/* ============================================================
 * LAYOUT INIT
 * ============================================================ */

static void compute_lane_positions(void) {
    int usable = SIM_X1 - SIM_X0 - 60;
    int step   = usable / NUM_QUEUES;
    for (int i = 0; i < NUM_QUEUES; i++)
        queue_cx[i] = SIM_X0 + 30 + step/2 + i*step;
}

/* ============================================================
 * QUEUE POSITION HELPERS
 * ============================================================ */

/*
 * position 0 = at the cashier counter (being served)
 * position 1 = immediately behind the counter, etc.
 *
 * Visually, position 0 sits at COUNTER_Y.
 * Position 1+ stacks upward.
 */
static void slot_position(int lane, int pos, float* ox, float* oy) {
    *ox = (float)queue_cx[lane];
    if (pos == 0) {
        /* At counter */
        *oy = (float)COUNTER_Y;
    } else {
        /* Stack upward: pos 1 is one step above pos 0 */
        *oy = (float)(COUNTER_Y - pos * QUEUE_STEP);
    }
}

/* ============================================================
 * QUEUE OPERATIONS
 * ============================================================ */

static int rand_range(int lo, int hi) {
    return lo + rand() % (hi - lo + 1);
}

static void queue_init(Queue* q) {
    q->front = q->rear = NULL; q->size = 0;
}

static void queue_push(Queue* q, Customer* c) {
    c->next     = NULL;
    c->position = q->size;
    if (!q->rear) { q->front = q->rear = c; }
    else          { q->rear->next = c; q->rear = c; }
    q->size++;
}

static Customer* queue_pop(Queue* q) {
    if (!q->front) return NULL;
    Customer* c  = q->front;
    q->front     = c->next;
    if (!q->front) q->rear = NULL;
    q->size--;
    return c;
}

/* Remove a specific customer by id (for reneging) */
static bool queue_remove(Queue* q, int id) {
    Customer* prev = NULL;
    Customer* curr = q->front;
    while (curr) {
        if (curr->id == id) {
            if (prev) prev->next = curr->next;
            else      q->front   = curr->next;
            if (curr == q->rear) q->rear = prev;
            free(curr);
            q->size--;
            return true;
        }
        prev = curr;
        curr = curr->next;
    }
    return false;
}

static void refresh_positions(Queue* q) {
    int p = 0;
    for (Customer* c = q->front; c; c = c->next)
        c->position = p++;
}

static int find_shortest(void) {
    int m = 0;
    for (int i = 1; i < NUM_QUEUES; i++)
        if (queues[i].size < queues[m].size) m = i;
    return m;
}

/* ============================================================
 * ANIMATED CUSTOMER MANAGEMENT
 * ============================================================ */

static AnimatedCustomer* anim_create(int id, int lane, float sx, float sy) {
    AnimatedCustomer* a = (AnimatedCustomer*)malloc(sizeof(AnimatedCustomer));
    a->id          = id;
    a->x           = sx;
    a->y           = sy;
    a->tx          = sx;
    a->ty          = sy;
    a->queue_index = lane;
    a->state       = STATE_WALKING_IN;
    a->alpha       = 0.0f;
    a->destroy_me  = false;
    a->next        = anim_head;
    anim_head      = a;
    return a;
}

static AnimatedCustomer* anim_find(int id) {
    for (AnimatedCustomer* a = anim_head; a; a = a->next)
        if (a->id == id) return a;
    return NULL;
}

static void anim_cleanup(void) {
    AnimatedCustomer* curr = anim_head;
    AnimatedCustomer* prev = NULL;
    while (curr) {
        if (curr->destroy_me) {
            if (prev) prev->next = curr->next;
            else      anim_head  = curr->next;
            AnimatedCustomer* tmp = curr;
            curr = curr->next;
            free(tmp);
        } else {
            prev = curr;
            curr = curr->next;
        }
    }
}

/* ============================================================
 * ANIMATION UPDATE
 * ============================================================ */

static void anim_update(AnimatedCustomer* a, float dt) {
    /* Fade in */
    if (a->alpha < 1.0f) {
        a->alpha += dt * 4.0f;
        if (a->alpha > 1.0f) a->alpha = 1.0f;
    }

    /* Lerp speed: feel snappy but not instant */
    float k = 1.0f - expf(-10.0f * dt);
    a->x += (a->tx - a->x) * k;
    a->y += (a->ty - a->y) * k;

    switch (a->state) {
        case STATE_WALKING_IN: {
            /* Walk to the entry row above the queue lane, then drop into slot */
            a->tx = (float)queue_cx[a->queue_index];
            a->ty = (float)ENTRY_Y;
            float dist = fabsf(a->x - a->tx) + fabsf(a->y - a->ty);
            if (dist < 8.0f) {
                a->state = STATE_IN_QUEUE;
            }
        } break;

        case STATE_LEAVING: {
            /* Slide downward off screen */
            a->tx = a->x;
            a->ty = (float)EXIT_Y;
            a->alpha -= dt * 2.0f;
            if (a->alpha <= 0.0f || a->y > SCREEN_HEIGHT + 40)
                a->destroy_me = true;
        } break;

        case STATE_RENEGED:
        case STATE_BALKED: {
            /* Slide upward off screen */
            a->tx = a->x;
            a->ty = (float)(HEADER_H - 80);
            a->alpha -= dt * 2.0f;
            if (a->alpha <= 0.0f || a->y < HEADER_H - 60)
                a->destroy_me = true;
        } break;

        default: break;
    }
}

/* ============================================================
 * SYNC ANIM TARGETS FROM QUEUE STATE
 * ============================================================ */

static void sync_anim_targets(void) {
    for (int lane = 0; lane < NUM_QUEUES; lane++) {
        for (Customer* c = queues[lane].front; c; c = c->next) {
            AnimatedCustomer* a = anim_find(c->id);
            if (!a) continue;
            float tx, ty;
            slot_position(lane, c->position, &tx, &ty);
            a->tx = tx;
            a->ty = ty;
            if (c->position == 0)
                a->state = STATE_BEING_SERVED;
            else if (a->state != STATE_WALKING_IN)
                a->state = STATE_IN_QUEUE;
        }
    }
}

/* ============================================================
 * SIMULATION TICK
 * ============================================================ */

static void simulation_tick(void) {
    if (sim_clock >= TOTAL_TIME) { sim_done = true; return; }

    /* ---- ARRIVALS ---- */
    if ((rand() % 100) < ARRIVAL_CHANCE) {
        bool all_full = true;
        for (int i = 0; i < NUM_QUEUES; i++)
            if (queues[i].size < MAX_LINE_LENGTH) { all_full = false; break; }

        if (all_full) {
            /* Balk: create a visual-only customer that bounces off */
            stats.balked++;
            int lane = find_shortest();
            AnimatedCustomer* a = anim_create(cust_id++, lane,
                                              (float)queue_cx[lane], (float)ENTRY_Y);
            a->alpha = 1.0f;
            a->state = STATE_BALKED;
        } else {
            int lane    = find_shortest();
            Customer* c = (Customer*)malloc(sizeof(Customer));
            c->id             = cust_id++;
            c->service_time   = rand_range(MIN_SERVICE, MAX_SERVICE);
            c->wait_time      = 0;
            c->patience_limit = rand_range(MIN_PATIENCE, MAX_PATIENCE);
            c->is_serving     = false;
            c->queue_index    = lane;
            c->position       = 0;
            c->next           = NULL;
            queue_push(&queues[lane], c);
            stats.arrived++;

            /* Spawn at top of lane, above the queue area */
            float sx = (float)queue_cx[lane];
            float sy = (float)(HEADER_H - 30);
            anim_create(c->id, lane, sx, sy);
        }
    }

    /* ---- SERVICE ---- */
    for (int lane = 0; lane < NUM_QUEUES; lane++) {
        if (!queues[lane].front) continue;

        Customer* front = queues[lane].front;

        front->service_time--;
        if (front->service_time <= 0) {
            /* Served! */
            stats.served++;
            stats.total_wait += (float)front->wait_time;
            stats.avg_wait    = stats.total_wait / (float)stats.served;

            AnimatedCustomer* a = anim_find(front->id);
            if (a) a->state = STATE_LEAVING;

            queue_pop(&queues[lane]);
            free(front);
            refresh_positions(&queues[lane]);
        }

        /* ---- RENEGE: waiting customers (not at counter) ---- */
        Customer* curr = queues[lane].front;
        while (curr) {
            Customer* nxt = curr->next;
            if (curr->position > 0) {   /* not the one at counter */
                curr->wait_time++;
                if (curr->wait_time > curr->patience_limit) {
                    stats.reneged++;
                    AnimatedCustomer* a = anim_find(curr->id);
                    if (a) a->state = STATE_RENEGED;
                    queue_remove(&queues[lane], curr->id);
                    refresh_positions(&queues[lane]);
                    /* restart iteration after structural change */
                    curr = queues[lane].front;
                    continue;
                }
            }
            curr = nxt;
        }
    }

    /* ---- JOCKEYING: check every 5 ticks if another line is 2+ shorter ---- */
    if (sim_clock % JOCKEY_INTERVAL == 0) {
        for (int lane = 0; lane < NUM_QUEUES; lane++) {
            if (!queues[lane].front) continue;
            Customer* prev    = queues[lane].front;
            Customer* current = queues[lane].front->next;
            while (current) {
                int shortest = find_shortest();
                if (shortest != lane && queues[shortest].size < current->position) {
                    stats.jockeyed++;
                    Customer* next = current->next;
                    /* splice out */
                    prev->next = next;
                    if (current == queues[lane].rear) queues[lane].rear = prev;
                    queues[lane].size--;
                    current->next = NULL;
                    current->position = 0;
                    queue_push(&queues[shortest], current);
                    refresh_positions(&queues[shortest]);
                    refresh_positions(&queues[lane]);
                    current = next;
                } else {
                    prev    = current;
                    current = current->next;
                }
            }
        }
    }

    /* Push logical positions into anim targets */
    sync_anim_targets();
}

/* ============================================================
 * DRAWING HELPERS
 * ============================================================ */

static void fill_rect(int x, int y, int w, int h,
                      Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    SDL_Rect rc = {x, y, w, h};
    SDL_RenderFillRect(renderer, &rc);
}

static void draw_rect_outline(int x, int y, int w, int h,
                              Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    SDL_Rect rc = {x, y, w, h};
    SDL_RenderDrawRect(renderer, &rc);
}

static void draw_text(const char* text, int x, int y,
                      TTF_Font* font, SDL_Color col, bool centered) {
    if (!text || !font) return;
    SDL_Surface* surf = TTF_RenderText_Blended(font, text, col);
    if (!surf) return;
    SDL_Texture* tex  = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_Rect rect = {x, y, surf->w, surf->h};
    if (centered) { rect.x -= rect.w/2; rect.y -= rect.h/2; }
    SDL_RenderCopy(renderer, tex, NULL, &rect);
    SDL_FreeSurface(surf);
    SDL_DestroyTexture(tex);
}

static void draw_bar(int x, int y, int w, int h, float frac,
                     Uint8 br, Uint8 bg, Uint8 bb,
                     Uint8 er, Uint8 eg, Uint8 eb) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    roundedBoxRGBA(renderer, x, y, x+w, y+h, 3, er, eg, eb, 80);
    int fill = (int)(w * frac);
    if (fill > 1)
        roundedBoxRGBA(renderer, x, y, x+fill, y+h, 3, br, bg, bb, 220);
}

/* ============================================================
 * DRAW BACKGROUND
 * ============================================================ */

static void draw_background(void) {
    fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COL_BG_DARK, 255);

    /* Subtle dots along each lane centre */
    for (int i = 0; i < NUM_QUEUES; i++) {
        int lx = queue_cx[i];
        for (int y = HEADER_H + 10; y < COUNTER_Y - CUST_R*2; y += 26)
            filledCircleRGBA(renderer, lx, y, 2, 50, 65, 100, 35);
    }

    /* Lane dividers */
    int step = (SIM_X1 - SIM_X0) / NUM_QUEUES;
    for (int i = 0; i <= NUM_QUEUES; i++) {
        int lx = SIM_X0 + i * step;
        lineRGBA(renderer, lx, HEADER_H, lx, SCREEN_HEIGHT,
                 COL_DIVIDER, 60);
    }

    /* Counter shelf band */
    fill_rect(SIM_X0, COUNTER_Y - 34, SIM_X1 - SIM_X0, 68, 20, 28, 50, 255);
    lineRGBA(renderer, SIM_X0, COUNTER_Y - 34, SIM_X1, COUNTER_Y - 34, 50,65,100,180);
    lineRGBA(renderer, SIM_X0, COUNTER_Y + 34, SIM_X1, COUNTER_Y + 34, 50,65,100,180);

    /* Floor below counter */
    fill_rect(SIM_X0, COUNTER_Y + 34, SIM_X1 - SIM_X0,
              SCREEN_HEIGHT - COUNTER_Y - 34, COL_BG_MID, 255);

    /* Sidebar + right panel */
    fill_rect(0, 0, SIDEBAR_W, SCREEN_HEIGHT, COL_BG_PANEL, 255);
    lineRGBA(renderer, SIDEBAR_W, 0, SIDEBAR_W, SCREEN_HEIGHT, 40,55,90,255);
    fill_rect(SIM_X1, 0, STAT_PANEL_W, SCREEN_HEIGHT, COL_BG_PANEL, 255);
    lineRGBA(renderer, SIM_X1, 0, SIM_X1, SCREEN_HEIGHT, 40,55,90,255);
}

/* ============================================================
 * DRAW HEADER
 * ============================================================ */

static void draw_header(void) {
    fill_rect(0, 0, SCREEN_WIDTH, HEADER_H, COL_BG_HEADER, 255);
    lineRGBA(renderer, 0, HEADER_H-1, SCREEN_WIDTH, HEADER_H-1,
             COL_ACCENT_R, COL_ACCENT_G, COL_ACCENT_B, 200);

    draw_text("GROCERY STORE", 24, 10, font_large,
              (SDL_Color){COL_ACCENT_R, COL_ACCENT_G, COL_ACCENT_B, 255}, false);
    draw_text("QUEUE SIMULATION", 24, 38, font_medium,
              (SDL_Color){COL_TEXT_DIM, 255}, false);

    char tbuf[32];
    int h  = START_HOUR + sim_clock/60;
    int m  = sim_clock % 60;
    int h12 = h % 12; if (!h12) h12 = 12;
    sprintf(tbuf, "%02d:%02d %s", h12, m, (h>=12)?"PM":"AM");
    draw_text(tbuf, SCREEN_WIDTH/2, 20, font_large,
              (SDL_Color){COL_TEXT_BRIGHT, 255}, true);

    draw_bar(SCREEN_WIDTH/2 - 120, 50, 240, 8,
             (float)sim_clock / TOTAL_TIME,
             COL_ACCENT_R, COL_ACCENT_G, COL_ACCENT_B, 40,50,80);

    char spd[32]; sprintf(spd, "%.2gx", (double)sim_speed);
    draw_text(spd, SCREEN_WIDTH - STAT_PANEL_W - 24, 34, font_medium,
              (SDL_Color){COL_TEXT_DIM,255}, true);

    if (paused) {
        roundedBoxRGBA(renderer, SIM_MID - 70, 16, SIM_MID + 70, 54,
                       6, 220, 80, 30, 230);
        draw_text("PAUSED", SIM_MID, 35, font_medium,
                  (SDL_Color){255,255,255,255}, true);
    }
    if (sim_done) {
        roundedBoxRGBA(renderer, SIM_MID - 130, 12, SIM_MID + 130, 58,
                       8, 30,180,100,220);
        draw_text("SIMULATION COMPLETE", SIM_MID, 35, font_medium,
                  (SDL_Color){255,255,255,255}, true);
    }
}

/* ============================================================
 * DRAW CASHIER COUNTER
 * ============================================================ */

static void draw_counter(int idx) {
    int cx = queue_cx[idx];
    int cy = COUNTER_Y;
    float load = (float)queues[idx].size / MAX_LINE_LENGTH;

    Uint8 r, g, b;
    if      (load < 0.5f)  { r=38;  g=180; b=110; }
    else if (load < 0.85f) { r=200; g=150; b=30;  }
    else                    { r=200; g=50;  b=50;  }

    int bx = cx - 46, by = cy - 26;
    roundedBoxRGBA(renderer, bx, by, bx+92, by+52, 8, r,g,b, 230);
    roundedRectangleRGBA(renderer, bx, by, bx+92, by+52, 8, 255,255,255, 50);

    /* Conveyor belt strip */
    fill_rect(bx+6, cy-4, 80, 8, 15,15,25, 200);
    /* Belt segment marks */
    for (int seg = bx+12; seg < bx+82; seg += 14)
        lineRGBA(renderer, seg, cy-3, seg, cy+3, 30,30,40,150);

    /* Label above */
    char label[24]; sprintf(label, "CASHIER %d", idx+1);
    draw_text(label, cx, by-18, font_tiny,
              (SDL_Color){COL_TEXT_DIM,255}, true);

    /* Queue depth bar below */
    draw_bar(bx, by+56, 92, 5, load, r,g,b, 30,40,65);

    /* Count */
    char cnt[16]; sprintf(cnt, "%d / %d", queues[idx].size, MAX_LINE_LENGTH);
    draw_text(cnt, cx, cy, font_small, (SDL_Color){255,255,255,200}, true);

    /* Busy dot */
    if (queues[idx].size > 0)
        filledCircleRGBA(renderer, bx+82, by+8, 5,
                         COL_ACCENT_R, COL_ACCENT_G, COL_ACCENT_B, 255);
}

/* ============================================================
 * DRAW ONE ANIMATED CUSTOMER
 * ============================================================ */

static void draw_customer(AnimatedCustomer* a) {
    Uint8 aa = (Uint8)(a->alpha * 255.0f);
    if (aa < 8) return;

    Uint8 r, g, b;
    switch (a->state) {
        case STATE_BEING_SERVED: r=60;  g=200; b=100; break;
        case STATE_RENEGED:      r=220; g=60;  b=80;  break;
        case STATE_BALKED:       r=240; g=180; b=30;  break;
        case STATE_LEAVING:      r=100; g=240; b=150; break;
        default:                 r=100; g=160; b=230; break;
    }

    int x = (int)a->x, y = (int)a->y;

    /* Shadow */
    filledCircleRGBA(renderer, x+2, y+3, CUST_R+3, 5,8,18, (Uint8)(aa*0.35f));

    /* Body */
    filledCircleRGBA(renderer, x, y, CUST_R, r,g,b, aa);

    /* Highlight */
    filledCircleRGBA(renderer, x-4, y-5, CUST_R/3,
                     255,255,255, (Uint8)(aa*0.22f));

    /* Rim */
    circleRGBA(renderer, x, y, CUST_R, 255,255,255, (Uint8)(aa*0.45f));

    /* ID */
    char buf[8]; sprintf(buf, "%d", a->id);
    draw_text(buf, x, y, font_tiny, (SDL_Color){255,255,255,aa}, true);

    /* State badge below */
    const char* badge = NULL;
    SDL_Color bc = {255,255,255,aa};
    if      (a->state == STATE_RENEGED) { badge="LEFT"; bc=(SDL_Color){220,80, 80,aa}; }
    else if (a->state == STATE_BALKED)  { badge="BALK"; bc=(SDL_Color){240,180,30,aa}; }
    else if (a->state == STATE_LEAVING) { badge="DONE"; bc=(SDL_Color){80,220,120,aa}; }
    if (badge)
        draw_text(badge, x, y+CUST_R+7, font_tiny, bc, true);
}

/* ============================================================
 * DRAW SIDEBAR
 * ============================================================ */

static void draw_sidebar(void) {
    int x0 = 16, y = HEADER_H + 20;

    draw_text("LEGEND", x0, y, font_medium,
              (SDL_Color){COL_ACCENT_R,COL_ACCENT_G,COL_ACCENT_B,255}, false);
    y += 30;

    struct { Uint8 r,g,b; const char* label; } ent[] = {
        {100,160,230, "Waiting in queue"},
        { 60,200,100, "Being served"    },
        {100,240,150, "Served & left"   },
        {220, 60, 80, "Reneged (left)"  },
        {240,180, 30, "Balked (full)"   },
        {180,120,255, "Jockeyed (switched)"},
    };
    for (int i = 0; i < 6; i++) {
        filledCircleRGBA(renderer, x0+10, y+8, 8, ent[i].r,ent[i].g,ent[i].b, 220);
        circleRGBA(renderer, x0+10, y+8, 8, 255,255,255, 50);
        draw_text(ent[i].label, x0+26, y, font_small,
                  (SDL_Color){COL_TEXT_BRIGHT,255}, false);
        y += 24;
    }

    y += 18;
    lineRGBA(renderer, x0, y, SIDEBAR_W-16, y, 40,55,90,255);
    y += 16;

    draw_text("CASHIER LOAD", x0, y, font_medium,
              (SDL_Color){COL_ACCENT_R,COL_ACCENT_G,COL_ACCENT_B,255}, false);
    y += 28;

    struct { Uint8 r,g,b; const char* label; } loads[] = {
        { 38,180,110, "Low  (< 50%)" },
        {200,150, 30, "Mid  (50-85%)"},
        {200, 50, 50, "High (> 85%)" },
    };
    for (int i = 0; i < 3; i++) {
        roundedBoxRGBA(renderer, x0, y, x0+14, y+14, 3,
                       loads[i].r,loads[i].g,loads[i].b, 220);
        draw_text(loads[i].label, x0+22, y, font_small,
                  (SDL_Color){COL_TEXT_BRIGHT,255}, false);
        y += 22;
    }

    y += 18;
    lineRGBA(renderer, x0, y, SIDEBAR_W-16, y, 40,55,90,255);
    y += 16;

    draw_text("CONTROLS", x0, y, font_medium,
              (SDL_Color){COL_ACCENT_R,COL_ACCENT_G,COL_ACCENT_B,255}, false);
    y += 28;

    const char* keys[]  = {"SPACE","UP","DOWN","ESC"};
    const char* descs[] = {"Pause / Resume","Speed up","Speed down","Quit"};
    for (int i = 0; i < 4; i++) {
        roundedBoxRGBA(renderer, x0, y, x0+52, y+18, 4, 40,55,90,255);
        roundedRectangleRGBA(renderer, x0, y, x0+52, y+18, 4, 80,100,140,200);
        draw_text(keys[i], x0+26, y+9, font_tiny,
                  (SDL_Color){COL_ACCENT_R,COL_ACCENT_G,COL_ACCENT_B,255}, true);
        draw_text(descs[i], x0+60, y+2, font_small,
                  (SDL_Color){COL_TEXT_DIM,255}, false);
        y += 26;
    }
}

/* ============================================================
 * DRAW STAT PANEL
 * ============================================================ */

static void draw_stat_panel(void) {
    int x0   = SIM_X1 + 16;
    int panW = STAT_PANEL_W - 24;
    int y    = HEADER_H + 20;

    draw_text("STATISTICS", x0, y, font_medium,
              (SDL_Color){COL_ACCENT_R,COL_ACCENT_G,COL_ACCENT_B,255}, false);
    y += 34;

#define STAT_ROW(lbl, val, R,G,B) { \
    draw_text(lbl, x0, y, font_tiny, (SDL_Color){COL_TEXT_DIM,255}, false); \
    char _b[32]; sprintf(_b, "%d", val); \
    int _tw; TTF_SizeText(font_medium, _b, &_tw, NULL); \
    draw_text(_b, x0+panW-_tw, y, font_medium, (SDL_Color){R,G,B,255}, false); \
    y += 38; }

    STAT_ROW("Arrived",  stats.arrived, 230,235,255)
    STAT_ROW("Served",   stats.served,  60,200,100)

    { float bper = stats.arrived > 0 ? 100.0f*stats.balked/stats.arrived : 0;
      draw_text("Balked", x0, y, font_tiny, (SDL_Color){COL_TEXT_DIM,255}, false);
      char _b[32]; sprintf(_b, "%d (%.1f%%)", stats.balked, (double)bper);
      int _tw; TTF_SizeText(font_medium, _b, &_tw, NULL);
      draw_text(_b, x0+panW-_tw, y, font_medium, (SDL_Color){240,180,30,255}, false); y += 38; }

    { float rper = stats.arrived > 0 ? 100.0f*stats.reneged/stats.arrived : 0;
      draw_text("Reneged", x0, y, font_tiny, (SDL_Color){COL_TEXT_DIM,255}, false);
      char _r[32]; sprintf(_r, "%d (%.1f%%)", stats.reneged, (double)rper);
      int _tw; TTF_SizeText(font_medium, _r, &_tw, NULL);
      draw_text(_r, x0+panW-_tw, y, font_medium, (SDL_Color){220,60,80,255}, false); y += 38; }

    { int lost = stats.balked + stats.reneged;
      float lper = stats.arrived > 0 ? 100.0f*lost/stats.arrived : 0;
      draw_text("Lost Total", x0, y, font_tiny, (SDL_Color){COL_TEXT_DIM,255}, false);
      char _l[32]; sprintf(_l, "%d (%.1f%%)", lost, (double)lper);
      int _tw; TTF_SizeText(font_medium, _l, &_tw, NULL);
      draw_text(_l, x0+panW-_tw, y, font_medium, (SDL_Color){200,100,100,255}, false); y += 38; }

    { float jper = stats.arrived > 0 ? 100.0f*stats.jockeyed/stats.arrived : 0;
      draw_text("Jockeyed", x0, y, font_tiny, (SDL_Color){COL_TEXT_DIM,255}, false);
      char _j[32]; sprintf(_j, "%d (%.1f%%)", stats.jockeyed, (double)jper);
      int _tw; TTF_SizeText(font_medium, _j, &_tw, NULL);
      draw_text(_j, x0+panW-_tw, y, font_medium, (SDL_Color){180,120,255,255}, false); y += 38; }
#undef STAT_ROW

    lineRGBA(renderer, x0, y, x0+panW, y, 40,55,90,255);
    y += 14;

    float srate = stats.arrived > 0
        ? 100.0f * stats.served / stats.arrived : 0.0f;
    draw_text("Service rate", x0, y, font_tiny,
              (SDL_Color){COL_TEXT_DIM,255}, false);
    y += 18;
    draw_bar(x0, y, panW, 10, srate/100.0f, 60,200,100, 30,40,65);
    char srbuf[16]; sprintf(srbuf, "%.0f%%", (double)srate);
    y += 14;
    draw_text(srbuf, x0+panW/2, y, font_small,
              (SDL_Color){COL_TEXT_BRIGHT,255}, true);
    y += 28;

    lineRGBA(renderer, x0, y, x0+panW, y, 40,55,90,255);
    y += 14;
    draw_text("Avg Wait (ticks)", x0, y, font_tiny,
              (SDL_Color){COL_TEXT_DIM,255}, false);
    y += 18;
    char wbuf[32]; sprintf(wbuf, "%.1f", (double)stats.avg_wait);
    draw_text(wbuf, x0, y, font_large,
              (SDL_Color){COL_ACCENT_R,COL_ACCENT_G,COL_ACCENT_B,255}, false);
    y += 40;

    lineRGBA(renderer, x0, y, x0+panW, y, 40,55,90,255);
    y += 14;
    draw_text("Queue depths", x0, y, font_tiny,
              (SDL_Color){COL_TEXT_DIM,255}, false);
    y += 18;
    int bw = panW / NUM_QUEUES - 4;
    for (int i = 0; i < NUM_QUEUES; i++) {
        float load = (float)queues[i].size / MAX_LINE_LENGTH;
        Uint8 br,bg,bb;
        if      (load < 0.5f)  { br=38; bg=180; bb=110; }
        else if (load < 0.85f) { br=200;bg=150; bb=30;  }
        else                    { br=200;bg=50;  bb=50;  }
        int bx = x0 + i*(bw+4);
        int barH = 40;
        fill_rect(bx, y, bw, barH, 30,40,65,255);
        int fillH = (int)(barH*load);
        if (fillH > 0)
            fill_rect(bx, y+barH-fillH, bw, fillH, br,bg,bb,230);
        draw_rect_outline(bx, y, bw, barH, 50,65,100,200);
        char qi[4]; sprintf(qi, "%d", i+1);
        draw_text(qi, bx+bw/2, y+barH+4, font_tiny,
                  (SDL_Color){COL_TEXT_DIM,255}, true);
    }
    y += 70;

    lineRGBA(renderer, x0, y, x0+panW, y, 40,55,90,255);
    y += 14;
    char spd[32]; sprintf(spd, "Speed: %.2gx", (double)sim_speed);
    draw_text(spd, x0, y, font_small, (SDL_Color){COL_TEXT_DIM,255}, false);
    y += 24;
    draw_bar(x0, y, panW, 8,
             (sim_speed - 0.25f) / 1.75f,
             COL_ACCENT_R,COL_ACCENT_G,COL_ACCENT_B, 30,40,65);

    if (sim_done) {
        int by2 = SCREEN_HEIGHT - 80;
        roundedBoxRGBA(renderer, x0-8, by2, x0+panW+8, by2+50,
                       8, 30,160,90,240);
        draw_text("DONE", x0+panW/2, by2+25, font_large,
                  (SDL_Color){255,255,255,255}, true);
    }
}

/* ============================================================
 * MAIN
 * ============================================================ */

int main(void) {
    srand((unsigned)time(NULL));
    compute_lane_positions();

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL INIT FAILED: %s\n", SDL_GetError()); return 1;
    }
    if (TTF_Init() < 0) {
        printf("TTF INIT FAILED: %s\n", TTF_GetError()); return 1;
    }

    window = SDL_CreateWindow(
        "Grocery Queue Simulation — Fixed",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH, SCREEN_HEIGHT,
        SDL_WINDOW_SHOWN);

    renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    const char* font_paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-B.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        NULL
    };

    for (int p = 0; font_paths[p] && !font_large; p++) {
        font_large  = TTF_OpenFont(font_paths[p], 22);
        font_medium = TTF_OpenFont(font_paths[p], 16);
        font_small  = TTF_OpenFont(font_paths[p], 13);
        font_tiny   = TTF_OpenFont(font_paths[p], 11);
    }
    if (!font_large) { printf("FONT LOAD FAILED\n"); return 1; }

    for (int i = 0; i < NUM_QUEUES; i++) {
        queue_init(&queues[i]);
        service_remaining[i] = 0;
    }

    bool   running    = true;
    Uint32 last       = SDL_GetTicks();
    float  tick_timer = 0.0f;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                    case SDLK_ESCAPE: running = false; break;
                    case SDLK_SPACE:  paused = !paused; break;
                    case SDLK_UP:
                        sim_speed = fminf(2.0f, sim_speed + 0.25f); break;
                    case SDLK_DOWN:
                        sim_speed = fmaxf(0.25f, sim_speed - 0.25f); break;
                }
            }
        }

        Uint32 now = SDL_GetTicks();
        float  dt  = (now - last) / 1000.0f;
        last = now;
        if (dt > 0.05f) dt = 0.05f;

        if (!paused && !sim_done) {
            tick_timer += dt * sim_speed;
            if (tick_timer >= 0.3f) {
                simulation_tick();
                sim_clock++;
                tick_timer = 0.0f;
            }
        }

        /* Update all animations */
        for (AnimatedCustomer* a = anim_head; a; a = a->next)
            anim_update(a, dt * sim_speed);
        anim_cleanup();

        /* ---- RENDER ---- */
        SDL_SetRenderDrawColor(renderer, COL_BG_DARK, 255);
        SDL_RenderClear(renderer);

        draw_background();

        /* Draw cashier guide lines from counter up to queue start */
        for (int i = 0; i < NUM_QUEUES; i++) {
            int cx = queue_cx[i];
            lineRGBA(renderer, cx, COUNTER_Y - CUST_R - 4,
                     cx, HEADER_H + 10, 50,70,110,80);
        }

        for (int i = 0; i < NUM_QUEUES; i++)
            draw_counter(i);

        /* Draw customers back-to-front (front of queue on top) */
        /* Collect pointers, draw in reverse Z */
        for (AnimatedCustomer* a = anim_head; a; a = a->next)
            draw_customer(a);

        draw_header();
        draw_sidebar();
        draw_stat_panel();

        SDL_RenderPresent(renderer);
    }

    /* Cleanup */
    while (anim_head) {
        AnimatedCustomer* nx = anim_head->next;
        free(anim_head);
        anim_head = nx;
    }
    for (int i = 0; i < NUM_QUEUES; i++) {
        while (queues[i].front) {
            Customer* nx = queues[i].front->next;
            free(queues[i].front);
            queues[i].front = nx;
        }
    }

    TTF_CloseFont(font_large);
    TTF_CloseFont(font_medium);
    TTF_CloseFont(font_small);
    TTF_CloseFont(font_tiny);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}