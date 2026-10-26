# ADR 0002: SDL2 for input, direct framebuffer for video

- **Status:** Accepted
- **Date:** 2026-10-26

## Context

TGMUOS runs on the RG35XX SP (muOS), an AArch64 device with a 640x480 LCD framebuffer at `/dev/fb0`. The device runs muOS with an older userspace (glibc ~2.38) and does not provide SDL2 development libraries or headers in its base image.

Two rendering approaches were considered:

1. **SDL2 video backend** (`SDL_CreateWindow`, `SDL_Renderer`, `SDL_RenderPresent`) — uses SDL2's abstraction for the display. Requires a working SDL2 video driver on the target (fbcon, kmsdrm, dummy, etc.) and SDL2 shared libraries present at runtime. Our cross-compiled SDL2 static library was built against glibc 2.43 (Fedora 44) and failed to initialize `SDL_INIT_VIDEO` on muOS's glibc 2.38, hanging on `SDL_Init(SDL_INIT_VIDEO)`.

2. **Direct framebuffer access** — open `/dev/fb0`, mmap the screen buffer, and write pixels directly using the `fb_var_screeninfo`/`fb_fix_screeninfo` ioctls. No SDL2 video backend required at runtime. SDL2 is used only for input (game controller / keyboard events) and SDL_ttf for font rasterization into an in-memory pixel buffer, which is then blitted to the framebuffer.

## Decision

Use a hybrid architecture:

- **Input/events:** SDL2 (`SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER`) maps D-pad and face buttons to logical key codes.
- **Video:** Direct Linux framebuffer (`/dev/fb0`) via `open/mmap/ioctl`. The renderer draws into a host-side `uint32_t` pixel canvas and converts to the framebuffer's native pixel format on `present()`.
- **Fonts:** SDL_ttf rasterizes glyphs into an RGBA surface, which is converted and copied into the renderer's pixel canvas.
- **Headless mode:** Supported for `--smoke-test` and `--frame-test` builds where `/dev/fb0` is unavailable; `renderer_present()` is a no-op.

## Consequences

- **Positive:** The app starts and renders without SDL2 video shared libraries on muOS. No dependency on SDL2 video driver availability or initialization. Works even if SDL2 video driver initialization hangs on the target kernel/userspace.
- **Negative:** The renderer must handle pixel format conversion manually (RGB888 to the framebuffer's native 16/24/32 bpp layout). No hardware acceleration, scaling, or vsync through SDL2; the framebuffer is written directly. Screen orientation and scaling are handled by muOS/firmware, not by the app.
- **Trade-off:** SDL2 remains a build-time and runtime dependency for input and font rasterization, but only `SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER` is initialized — not `SDL_INIT_VIDEO`.

## Validation required before release

1. Confirm `renderer_init` successfully opens and mmaps `/dev/fb0` on the RG35XX SP.
2. Confirm `renderer_present` copies pixels to the framebuffer and the display updates.
3. Confirm headless mode (`--smoke-test`, `--frame-test`) runs without `/dev/fb0`.
4. Confirm controller input via SDL2 game controller mapping works on the device.

## Related

- ADR 0001: TDLib JSON as the Telegram runtime
- README.md: Build and deployment instructions
