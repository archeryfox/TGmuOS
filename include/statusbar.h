#pragma once

#include "renderer.h"

/* Draw a compact status line at (x, y): time, battery, network and
 * proxy/VPN connection state. Caches sysfs reads for a few seconds. */
void ui_status_bar(AppRenderer* renderer, int x, int y);
