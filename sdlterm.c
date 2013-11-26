#include <stdio.h>
#include <unistd.h>
#include <util.h>
#include <sys/ioctl.h>

#include <SDL.h>
#include <SDL_image.h>

#include "fvemu.h"

#define CHAR_WIDTH  8
#define CHAR_HEIGHT 16

#define SCREEN_COLS 80
#define SCREEN_ROWS 25

#define HORZ_PADDING 8
#define VERT_PADDING 8

SDL_Window *win = NULL;
SDL_Renderer *ren = NULL;
SDL_Texture *font = NULL;
struct emuState tty;
int dirty = 1;
Uint32 PTY_EVENT = -1;
int tty_fd = -1;

struct ttychunk {
    int len;
    uint8_t data[0];
};

void sdl_die(const char *msg)
{
    fprintf(stderr, "%s: %s\n", msg, SDL_GetError());
    exit(1);
}



void TerminalEmulator_bell(struct emuState *S)
{
    printf("Beep!\a\n"); // FIXME
}

void TerminalEmulator_setTitle(struct emuState *S, const char *title)
{
    SDL_SetWindowTitle(win, title);
}

void TerminalEmulator_resize(struct emuState *S)
{
    // FIXME
}

void TerminalEmulator_write(struct emuState *S, char *bytes, size_t len)
{
    write(tty_fd, bytes, len);
}

void TerminalEmulator_writeStr(struct emuState *S, char *bytes)
{
    TerminalEmulator_write(S, bytes, strlen(bytes));
}

void TerminalEmulator_freeRowBitmaps(struct termRow *r)
{
    return;
}



int tty_reader(void *refcon)
{
    char buf[256];
    int n;
    while ((n = read(tty_fd, buf, sizeof(buf))) > 0) {
        struct ttychunk *chunk = malloc(sizeof(struct ttychunk) + n);
        chunk->len = n;
        memcpy(&chunk->data, buf, n);

        SDL_Event ev;
        ev.type = PTY_EVENT;
        ev.user.code = 0;
        ev.user.data1 = chunk;
        ev.user.data2 = NULL;
        SDL_PushEvent(&ev);
    }
    return 0;
}

void tty_init(int rows, int cols)
{
    struct winsize ws = {
        .ws_row = rows,
        .ws_col = cols,
        .ws_xpixel = 0,
        .ws_ypixel = 0,
    };

    int pid = forkpty(&tty_fd, NULL, NULL, &ws);
    if (pid < 0) {
        perror("forkpty");
        exit(1);
    }

    if (pid == 0) {
        setsid();
        setenv("TERM", "xterm", 1);

        struct passwd *pwd = getpwuid(getuid());
        char *shell = (pwd && pwd->pw_shell) ? pwd->pw_shell : "/bin/sh";
        endpwent();

        execl(shell, "-", NULL);
        printf("Failed to exec shell %s\n", shell);
        exit(255);
    }

    PTY_EVENT = SDL_RegisterEvents(1);

    if (!SDL_CreateThread(tty_reader, "tty_reader", NULL))
        sdl_die("SDL_CreateThread(tty_reader)");
}



void redraw(void)
{
    SDL_RenderClear(ren);

    SDL_assert(tty.wRows == SCREEN_ROWS);
    SDL_assert(tty.wCols == SCREEN_COLS);

    uint32_t *plt = tty.palette;

    for (int r = 0; r < SCREEN_ROWS; r++) {
        struct termRow *row = tty.rows[r];
        for (int c = 0; c < SCREEN_COLS; c++) {
            SDL_Rect src, dst;

            uint64_t cell = row->chars[c];
            uint8_t ch = cell & 0xff;

            src.x = CHAR_WIDTH  * (ch % 32);
            src.y = CHAR_HEIGHT * (ch / 32);

            dst.x = HORZ_PADDING + c * CHAR_WIDTH;
            dst.y = HORZ_PADDING + r * CHAR_HEIGHT;

            src.w = dst.w = CHAR_WIDTH;
            src.h = dst.h = CHAR_HEIGHT;

            SDL_RenderCopy(ren, font, &src, &dst);
        }
    }

    SDL_RenderPresent(ren);
}

int main(int argc, char **argv)
{
    if (SDL_Init(SDL_INIT_EVERYTHING) == -1)
        sdl_die("SDL_Init");

    win = SDL_CreateWindow(
            "sdlterm",
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            SCREEN_COLS * CHAR_WIDTH + 2 * HORZ_PADDING,
            SCREEN_ROWS * CHAR_HEIGHT + 2 * VERT_PADDING,
            0
            );
    if (!win)
        sdl_die("SDL_CreateWindow");

    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!ren)
        sdl_die("SDL_CreateRenderer");

    SDL_Surface *fontsurface = IMG_Load("font.png");
    if (!fontsurface)
        sdl_die("IMG_Load(font.png)");
    font = SDL_CreateTextureFromSurface(ren, fontsurface);
    SDL_FreeSurface(fontsurface);
    fontsurface = NULL;

    emu_core_init(&tty, SCREEN_ROWS, SCREEN_COLS);

    tty_init(SCREEN_ROWS, SCREEN_COLS);

    SDL_StartTextInput();

    for (;;) {
        if (dirty) {
            redraw();
            dirty = 0;
        }

        SDL_Event e;
        if (SDL_WaitEvent(&e)) {
            if (e.type == SDL_QUIT) {
                break;
            } else if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        TerminalEmulator_writeStr(&tty, "\e");
                        break;
                    case SDLK_BACKSPACE:
                        TerminalEmulator_writeStr(&tty, "\x7f");
                        break;
                    case SDLK_UP:
                        TerminalEmulator_writeStr(&tty, "\e[A");
                        break;
                    case SDLK_DOWN:
                        TerminalEmulator_writeStr(&tty, "\e[B");
                        break;
                    case SDLK_LEFT:
                        TerminalEmulator_writeStr(&tty, "\e[C");
                        break;
                    case SDLK_RIGHT:
                        TerminalEmulator_writeStr(&tty, "\e[D");
                        break;
                    case SDLK_RETURN:
                    case SDLK_RETURN2:
                        TerminalEmulator_writeStr(&tty, "\r");
                        break;
                }
            } else if (e.type == SDL_TEXTINPUT) {
                TerminalEmulator_writeStr(&tty, e.text.text);
            } else if (e.type == PTY_EVENT) {
                struct ttychunk *chunk = e.user.data1;
                dirty += emu_core_run(&tty, chunk->data, chunk->len);
                free(chunk);
            }
        }
    }

    SDL_Quit();
    return 0;
}
