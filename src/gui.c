struct window {
        char *title;
        int   width;
        int   height;
        int   scale;
        u32  *buf;
        bool  shown;

        SDL_Renderer *renderer;
        SDL_Texture  *texture;
        SDL_Window   *window;
};

static noreturn void sdl_fail(void)
{
        assert(0);
        die("SDL fail: %s\n", SDL_GetError());
}

static void init_window(struct window *w)
{
        w->window = SDL_CreateWindow(w->title,
                                     w->width * w->scale,
                                     w->height * w->scale,
                                     SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE);
        if (w->window == NULL)
                sdl_fail();

        w->renderer = SDL_CreateRenderer(w->window, NULL);
        if (w->renderer == NULL)
                sdl_fail();

        w->texture = SDL_CreateTexture(w->renderer,
                                       SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STREAMING,
                                       w->width,
                                       w->height);
        if(w->texture == NULL)
                sdl_fail();

	if (!SDL_SetTextureScaleMode(w->texture, SDL_SCALEMODE_NEAREST))
		sdl_fail();

	if (!SDL_SetRenderLogicalPresentation(w->renderer, w->width, w->height, SDL_LOGICAL_PRESENTATION_LETTERBOX))
		sdl_fail();

        w->shown = true;
}

static void render_window(struct window *w)
{
        int pitch;
        int *pixels;

        if (!SDL_LockTexture(w->texture, NULL, (void **)&pixels, &pitch))
                sdl_fail();

        memcpy(pixels, w->buf, (size_t)(w->width * w->height) * sizeof *w->buf);

        SDL_UnlockTexture(w->texture);

        if (!SDL_RenderClear(w->renderer))
                sdl_fail();

        if (!SDL_RenderTexture(w->renderer, w->texture, NULL, NULL))
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
	/* TODO(sd3): SDL_WINDOW_SHOWN has been removed */
        /* else if (flags & SDL_WINDOW_SHOWN) */
        /*         SDL_HideWindow(w->window); */
        /* else */
        /*         SDL_ShowWindow(w->window); */
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

/* TODO: don't use sleep to ensure framerate as it's unpredictable */

static u64 clock_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
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
