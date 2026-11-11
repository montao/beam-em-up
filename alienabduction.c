/*
 * Alien Abduction (Pasture Raid), a terminal game for Unix.
 * Build: cc -std=c99 -O2 alienabduction.c -o alienabduction
 * Play:  ./alienabduction [--ascii]
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 montao
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

enum { WIDTH = 30, GROUND = 6, HERD = 4, GOAL = 6, TICKS = 450 };
struct animal { int x, direction, goat, pace; };
struct game {
    struct animal herd[HERD];
    int x, beam, target, lift, cows, goats, tick;
};
static struct termios saved;
static int active;
static volatile sig_atomic_t stop;

static void cleanup(void) {
    if (!active) return;
    tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    fputs("\033[?25h\033[?1049l", stdout);
    fflush(stdout);
    active = 0;
}

static void interrupted(int sig) { stop = sig; }

static long long ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void spawn(struct game *g, int i) {
    struct animal *a = &g->herd[i];
    int j;
    do {
        a->x = rand() % WIDTH;
        for (j = 0; j < HERD; ++j)
            if (j != i && g->herd[j].x == a->x) break;
    } while (j != HERD);
    a->goat = rand() % 2;
    a->direction = rand() % 2 ? 1 : -1;
    a->pace = 3 + rand() % 5;
}

static void reset(struct game *g) {
    memset(g, 0, sizeof *g);
    g->x = WIDTH / 2;
    g->target = -1;
    for (int i = 0; i < HERD; ++i) g->herd[i].x = -1;
    for (int i = 0; i < HERD; ++i) spawn(g, i);
    g->herd[0].goat = 0;
    g->herd[1].goat = 1;
}

static void move(struct game *g, int direction) {
    int x = g->x + direction;
    if (x >= 0 && x < WIDTH) g->x = x;
    if (g->target >= 0 && g->herd[g->target].x != g->x) {
        g->target = -1;
        g->lift = 0;
    }
}

static void update(struct game *g) {
    ++g->tick;
    if (g->beam) {
        if (g->target < 0)
            for (int i = 0; i < HERD; ++i)
                if (g->herd[i].x == g->x) { g->target = i; break; }
        if (g->target >= 0 && ++g->lift == GROUND + 1) {
            if (g->herd[g->target].goat) ++g->goats;
            else ++g->cows;
            spawn(g, g->target);
            g->target = -1;
            g->lift = 0;
        }
    }
    for (int i = 0; i < HERD; ++i) {
        struct animal *a = &g->herd[i];
        if (i == g->target || (g->tick + i) % a->pace) continue;
        int x = a->x + a->direction;
        if (x < 0 || x >= WIDTH) { a->direction *= -1; x = a->x + a->direction; }
        int j;
        for (j = 0; j < HERD; ++j)
            if (j != i && g->herd[j].x == x) break;
        if (j == HERD) a->x = x;
        else a->direction *= -1;
    }
}

static int fits(void) {
    struct winsize size;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) != 0) return 1;
    return (!size.ws_col || size.ws_col >= 63) &&
           (!size.ws_row || size.ws_row >= 14);
}

static void draw(const struct game *g, int ascii, int fit) {
    int total = g->cows + g->goats;
    fputs("\033[H", stdout);
    if (!fit) {
        fputs("Resize to 63 x 14 to resume.\033[K\r\nQ quits.\033[J", stdout);
        fflush(stdout);
        return;
    }
    printf(" PASTURE RAID  |  Cargo %d/%d  |  Time %2ds\033[K\r\n"
           " Cows: %d   Goats: %d\033[K\r\n",
           total, GOAL, (TICKS - g->tick + 9) / 10, g->cows, g->goats);
    for (int y = 0; y <= GROUND; ++y) {
        putchar('|');
        for (int x = 0; x < WIDTH; ++x) {
            const char *cell = "  ";
            if (x == g->x) {
                if (y == 0) cell = ascii ? "<>" : "🛸";
                else if (g->beam) cell = "||";
            }
            for (int i = 0; i < HERD; ++i) {
                int row = GROUND - (i == g->target ? g->lift - 1 : 0);
                if (g->herd[i].x == x && y == row)
                    cell = g->herd[i].goat ? (ascii ? "G " : "🐐") :
                                            (ascii ? "C " : "🐄");
            }
            fputs(cell, stdout);
        }
        fputs("|\033[K\r\n", stdout);
    }
    putchar('+');
    for (int x = 0; x < WIDTH * 2; ++x) putchar('-');
    fputs("+\033[K\r\n", stdout);
    if (total >= GOAL)
        fputs(" Cargo full! Mission complete.  R replay  Q quit", stdout);
    else if (g->tick >= TICKS)
        fputs(" Time's up!  R replay  Q quit", stdout);
    else
        printf(" A/D or arrows: fly   Space: beam [%s]   R restart   Q quit",
               g->beam ? "on" : "off");
    fputs("\033[K\033[J", stdout);
    fflush(stdout);
}

int main(int argc, char **argv) {
    int ascii = argc == 2 && !strcmp(argv[1], "--ascii");
    if (argc == 2 && !strcmp(argv[1], "--help")) {
        puts("Usage: alienabduction [--ascii]\nFly with A/D or left/right arrows. Space toggles the tractor beam.\n"
             "Hover over a cow or goat until it lifts into the UFO. Collect six in 45 seconds.\n"
             "R restarts. Q quits. Use --ascii for terminals with uneven emoji spacing.");
        return 0;
    }
    if (argc != 1 && !ascii) { fputs("Usage: alienabduction [--ascii]\n", stderr); return 2; }
    setlocale(LC_CTYPE, "");
    if (!isatty(STDOUT_FILENO) || tcgetattr(STDIN_FILENO, &saved) == -1 ||
        !getenv("TERM") || !strcmp(getenv("TERM"), "dumb")) {
        fputs("alienabduction needs an interactive terminal. Over SSH, use ssh -t.\n", stderr);
        return 1;
    }
    struct sigaction sa = {0};
    sa.sa_handler = interrupted;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    struct termios raw = saved;
    raw.c_lflag &= ~(ICANON | ECHO | IEXTEN | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    atexit(cleanup);
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("alienabduction: terminal setup");
        return 1;
    }
    active = 1;
    fputs("\033[?1049h\033[?25l\033[2J", stdout);
    srand((unsigned)(time(NULL) ^ getpid()));
    struct game g;
    reset(&g);
    int quit = 0, esc = 0, dirty = 1, fit = fits(), status = 0;
    long long next = ms() + 100;
    while (!quit && !stop) {
        long long now = ms();
        if (now >= next) {
            int current_fit = fits();
            if (current_fit != fit) { fit = current_fit; dirty = 1; }
            if (fit && g.tick < TICKS && g.cows + g.goats < GOAL) {
                update(&g);
                dirty = 1;
            }
            next = now + 100;
        }
        if (dirty) { draw(&g, ascii, fit); dirty = 0; }
        long long remaining = next - ms();
        struct pollfd p = { STDIN_FILENO, POLLIN, 0 };
        int ready = poll(&p, 1, remaining > 0 ? (int)remaining : 0);
        if (ready < 0) { if (errno == EINTR) continue; status = 1; break; }
        if (p.revents & (POLLHUP | POLLERR | POLLNVAL)) break;
        if (!(p.revents & POLLIN)) continue;
        unsigned char keys[128];
        ssize_t n = read(STDIN_FILENO, keys, sizeof keys);
        if (n < 0) { if (errno == EINTR) continue; status = 1; break; }
        for (ssize_t i = 0; i < n && !quit; ++i) {
            int c = keys[i];
            if (c == 3 || c == 4 || c == 'q' || c == 'Q') { quit = 1; break; }
            int direction = 0;
            if (c == 27) { esc = 1; continue; }
            if (esc == 1 && (c == '[' || c == 'O')) { esc = 2; continue; }
            if (esc == 2) {
                if (c == 'D') direction = -1;
                if (c == 'C') direction = 1;
                esc = 0;
                c = 0;
            } else esc = 0;
            if (c == 'r' || c == 'R') { reset(&g); dirty = 1; next = ms() + 100; }
            if (!fit || g.tick >= TICKS || g.cows + g.goats >= GOAL) continue;
            if (c == 'a' || c == 'A') direction = -1;
            if (c == 'd' || c == 'D') direction = 1;
            if (direction) { move(&g, direction); dirty = 1; }
            if (c == ' ') { g.beam = !g.beam; g.target = -1; g.lift = 0; dirty = 1; }
        }
    }
    return stop ? 128 + stop : status;
}
