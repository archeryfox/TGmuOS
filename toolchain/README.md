# tgmuos-toolchain
Сборочная среда для tgmuos (Telegram-клиент для Anbernic RG35XX SP / muOS).

## Содержание

- `Dockerfile` — многоэтапный Docker-образ для сборки бинарника под aarch64.
- `build-muos.sh` — основной скрипт сборки.
- `build-tdlib-arm64.sh` — сборка TDLib (libtdjson.so) из исходников.
- `deploy-app.sh` — разработка бинарника на устройство через adb.

## Требования

- Docker.
- SDL2/SDL2_ttf для aarch64 (заголовки + .so). Положите в `sdk/`:
  - `sdk/include/SDL2/SDL.h`
  - `sdk/include/SDL2/SDL_ttf.h`
  - `sdk/lib/libSDL2.so`
  - `sdk/lib/libSDL2_ttf.so`

## Сборка

```bash
docker build -t tgmuos-builder .
docker run --rm -v $(pwd):/workspace tgmuos-builder
```

Результат: `out/tgmuos` — бинарник для aarch64 (glibc 2.43, совместим с muOS glibc 2.38).

## Деплой

```bash
bash toolchain/deploy-app.sh out/tgmuos
```

## Устройство

- IP: 192.168.0.151, SSH root/root.
- Приложение: `/mnt/mmc/MUOS/application/Telegram/`.
- `.muxzip`: `/mnt/mmc/ARCHIVE/tgmuos.muxzip`.
- Логи: `/mnt/mmc/MUOS/application/Telegram/log.txt`.

## Настройка Telegram

Создайте `telegram.conf` в корне приложения:

```
api_id=39311583
api_hash=cf10bc1f7475e0ceaf2d7e219455b6aa
```

Зарегистрируйте свои `api_id` и `api_hash` на [my.telegram.org/apps](https://my.telegram.org/apps).

## Прокси

Для использования VLESS-прокси запустите `tools/import_vless_subscription.py` и убедитесь, что `sing-box` слушает SOCKS5 на `127.0.0.1:10808`.

## Архитектурные решения (ADR)

- `docs/adr/0001-tdlib-json-for-muos.md` — TDLib C/JSON интерфейс.
- `docs/adr/0002-sdl2-input-direct-framebuffer.md` — рендеринг через /dev/fb0.
- `docs/adr/0003-vless-sing-box-proxy-for-tdlib.md` — VLESS-прокси.
- `docs/adr/0004-muvpn-tun-vpn-app.md` — muVPN scaffold.