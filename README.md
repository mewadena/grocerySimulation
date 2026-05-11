# Grocery Store Queue Simulation

## BIC10403 Data Structure — Project 4 | Group 1

**Members:**
- Nur Ameera Adriana (CI250060)
- Alya Nurjannah (CI250082)
- Muhammad Faris Hakimi (CI250125)
- Muhammad Firdaus Hakimi (CI250086)
- Nur Atiqa Najwa (CI250145)

---

## Files

| File | Description |
|---|---|
| `grocery_simulation_json.c` | Console-based simulation (Windows) |
| `grocery_visual.c` | Graphical SDL2 simulation (Linux/WSL) |

---

## How to Compile & Run

### Console Simulation (Windows)

Compile with any C compiler:

```
gcc grocery_simulation_json.c -o grocery_simulation_json.exe
```

Run:

```
./grocery_simulation_json.exe
```

The program will ask for:
- Max queue length per line (default: 5)
- Store operating hours (default: 8 hours)

### Visual Simulation (Linux / WSL)

Requires SDL2, SDL2_ttf, and SDL2_gfx libraries:

```bash
# Install dependencies
sudo apt install libsdl2-dev libsdl2-ttf-dev libsdl2-gfx-dev

# Compile
gcc grocery_visual.c -o grocery_visual -lSDL2 -lSDL2_ttf -lSDL2_gfx -lm

# Run
./grocery_visual
```

**Controls:**
- `SPACE` — Pause / Resume
- `UP` — Increase speed (max 2x)
- `DOWN` — Decrease speed (min 0.25x)
- `R` — Reset simulation
- `ESC` — Exit

---

## What the Simulation Does

Five cashier lines serve customers who arrive at random intervals. Each customer:

1. **Arrives** — chooses the shortest line (first available if tied)
2. **Waits** — stays in line unless they lose patience
3. **Gets served** — if they reach the front, checkout takes a random amount of time
4. **May balk** — if all lines are full when they arrive, they leave immediately
5. **May reneg** — if they wait longer than their patience limit, they abandon the queue
6. **May jockey** — every 5 ticks, waiting customers check if another line is shorter and switch if it improves their position

### Simulation Parameters (visual version constants)

| Parameter | Value | Description |
|---|---|---|
| `NUM_QUEUES` | 5 | Number of cashier lines |
| `MIN_SERVICE` | 8 | Minimum checkout time (ticks) |
| `MAX_SERVICE` | 16 | Maximum checkout time (ticks) |
| `MIN_PATIENCE` | 15 | Minimum customer patience (ticks) |
| `MAX_PATIENCE` | 60 | Maximum customer patience (ticks) |
| `ARRIVAL_CHANCE` | 78% | Probability a customer arrives each tick |
| `MAX_LINE_LENGTH` | 6 | Max customers per line before balking |

---

## C Language Elements Used

### Data Structures

| Concept | Where | How |
|---|---|---|
| **Struct** | `Customer`, `Queue`, `AnimatedCustomer`, `Stats` | Custom data types grouping related fields |
| **Singly Linked List** | Each `Queue` is a linked list of `Customer` nodes | `front`/`rear` pointers, `next` pointer per node |
| **Array** | `queues[NUM_QUEUES]` | Fixed-size array of 5 queues simulating multiple cashier lines |
| **Enum** | `CustomerState` (visual only) | Named constants for animation states (balked, reneged, served, etc.) |

### Algorithms & Logic

| Concept | Where | Description |
|---|---|---|
| **Queue operations** | `queue_push`, `queue_pop`, `queue_remove` | Enqueue to rear, dequeue from front, remove by ID from anywhere in the list |
| **Random number generation** | `rand()`, `rand_range()` | Stochastic arrival times, service times, patience limits |
| **Linear search** | `find_shortest_queue()` | Scans all 5 queues to find the one with fewest customers |
| **Balking** | `phase_arrival()` | Customer leaves if every queue is at max capacity |
| **Reneging** | `phase_reneging()` | Customer leaves if `wait_time > patience_limit` |
| **Jockeying** | `phase_jockeying()` | Customer switches lines if another is shorter than their current position |
| **Timer-based tick loop** | Main loop (`simulation_tick`) | 1 tick = 1 simulation minute |

### Core C Features

| Feature | Usage |
|---|---|
| **Pointers** | Linked list node connections (`Customer* next`), dynamic allocation (`malloc`/`free`) |
| **Dynamic memory** | `malloc` for new customers, `free` when served or reneged |
| **Typedef** | `typedef struct Customer Customer` — cleaner type names |
| **Function pointers** | (Not used — direct function calls throughout) |
| **Preprocessor macros** | `#define` for constants, `STAT_ROW` macro for stats display |
| **Randomness** | `rand()`, `srand(time(NULL))` for stochastic behavior |
| **Console I/O** | `printf`, `gotoxy`, coloured output via Windows API (console version) |
| **Graphics** | SDL2 rendering, TTF fonts, geometric primitives (visual version) |
| **Linked list traversal** | While loops following `next` pointers for queue iteration |
| **Modular arithmetic** | `clock % JOCKEY_INTERVAL` for periodic jockeying checks |
