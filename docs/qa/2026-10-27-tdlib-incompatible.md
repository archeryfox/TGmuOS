# QA-2026-10-27-001: TDLib устарела — клиент не может авторизоваться

- **Статус:** найдено тестировщиком, передано фиксеру
- **Серьёзность:** blocker
- **Компонент:** runtime-зависимость `lib/libtdjson.so` (TDLib v1.8.0)

## Симптом

Приложение на устройстве стартует штатно (framebuffer открыт, первый кадр
отрисован, контроллер виден), но TDLib неработоспособна.

## Доказательства

Устройство `anbernic`, `/mnt/mmc/MUOS/application/tgmuos/log.txt`:

```
tgmuos starting
External keyboards detected: 0
Opened controller: muOS-Keys
Framebuffer: 640x480, 32 bpp, stride 2560
Using font: res/fonts/AdwaitaSans-Regular.ttf
UI ready; press ESC to quit
First frame rendered on screen 0
TDLib: This TDLib version is incompatible; rebuild it for the current API
```

Процессы на момент проверки: `mux_launch.sh` → `run-client.sh` →
`sing-box run` + `tgmuos` (PID 23488) — все живы, sing-box поднял SOCKS5.

## Причина

TDLib v1.8.0 (собрана 2026-10-26 для AArch64) отвергнута текущим API
Telegram. Диагностика из сессии «Fixing Telegram Startup» (mismatch
`setTdlibParameters`) устранялась под схему НОВЫХ версий TDLib — фактически
бинарник v1.8.0 не соответствует схеме и серверу одновременно.

## Ожидаемое поведение

После старта в логе: загрузка libtdjson.so без ошибки, состояние
`authorizationStateWaitPhoneNumber` (или QR). На экране — экран входа,
а не вечный статус инициализации.

## План верификации (тестировщик)

1. Фиксер выкатывает свежую TDLib (≥ v1.8.46) для AArch64 + пересобранный
   бинарник; бэкапы текущих файлов с меткой времени.
2. Перезапуск клиента через штатный launcher; ожидание ≥ 60 c.
3. Acceptance: в `log.txt` нет строки `version is incompatible`; есть
   признак загрузки TDLib и ожидания авторизации; UI показывает экран входа.
4. Regression: host ctest 7/7; бинарник AArch64, GLIBC ≤ 2.38,
   зависимости только glibc/libm.
5. Конфигурационная проверка: `telegram.conf` на устройстве с правами 600
   (на момент находки: `-rwxr-xr-x` — дефект доступа, QA-2026-10-27-002).

## QA-2026-10-27-002: telegram.conf доступен на чтение всем

`/root/.config/tgmuos/telegram.conf` имеет права `755` при требуемых `600`
(содержит api_id/api_hash). Устраняется вместе с выкладкой фикса.
