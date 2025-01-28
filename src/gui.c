static noreturn void sdl_fail(void)
{
        assert(0);
        die("SDL fail: %s\n", SDL_GetError());
}

static void init_window(struct window *w)
{
        w->window = SDL_CreateWindow(w->title,
                                     SDL_WINDOWPOS_UNDEFINED,
                                     SDL_WINDOWPOS_UNDEFINED,
                                     w->width * w->scale,
                                     w->height * w->scale,
                                     /* SDL_WINDOW_ALWAYS_ON_TOP | */
                                     SDL_WINDOW_ALLOW_HIGHDPI);
        if (w->window == NULL)
                sdl_fail();

        w->renderer = SDL_CreateRenderer(w->window, -1, 0);
        if (w->renderer == NULL)
                sdl_fail();

        w->texture = SDL_CreateTexture(w->renderer,
                                       SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STREAMING,
                                       w->width,
                                       w->height);
        if(w->texture == NULL)
                sdl_fail();

        w->shown = true;
}

static void render_window(struct window *w)
{
        int pitch;
        int *pixels;

        if (SDL_LockTexture(w->texture, NULL, (void **)&pixels, &pitch) < 0)
                sdl_fail();

        memcpy(pixels, w->buf, (size_t)(w->width * w->height) * sizeof *w->buf);

        SDL_UnlockTexture(w->texture);

        if (SDL_RenderClear(w->renderer) < 0)
                sdl_fail();

        if (SDL_RenderCopy(w->renderer, w->texture, NULL, NULL) < 0)
                sdl_fail();

        SDL_RenderPresent(w->renderer);
}

static void close_window(struct window *w)
{
        SDL_DestroyTexture(w->texture);
        SDL_DestroyRenderer(w->renderer);
        SDL_DestroyWindow(w->window);
}

static void toggle_window_shown(struct window *w)
{
        u32 flags = SDL_GetWindowFlags(w->window);

        if (!flags)
                init_window(w);
        else if (flags & SDL_WINDOW_SHOWN)
                SDL_HideWindow(w->window);
        else
                SDL_ShowWindow(w->window);
}

static void rescale_window(struct window *w, int scale)
{
        SDL_SetWindowSize(w->window, w->width * scale, w->height * scale);
        w->scale = scale;
}

static bool window_focused(struct window *w)
{
        return SDL_GetWindowFlags(w->window) & SDL_WINDOW_INPUT_FOCUS;
}

static u64 clock_ns(void)
{
        return clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
}

static void sleep_ns(u64 ns)
{
        struct timespec t, ignore;

        u64 target = clock_ns() + ns;

        if (ns > 5 * 1000 * 1000) {
                ns -= 1000 * 1000;

                t.tv_sec  = ns / (1000 * 1000 * 1000);
                t.tv_nsec = ns % (1000 * 1000 * 1000);

                nanosleep(&t, &ignore);
        }

        while(clock_ns() < target)
                ;
}
