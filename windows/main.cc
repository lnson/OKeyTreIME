// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include <windows.h>

#include "windows/app.h"

int WINAPI wWinMain(HINSTANCE /*hinstance*/, HINSTANCE /*hprev_instance*/,
                    PWSTR /*cmd_line*/, int /*show_cmd*/) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  okeytre::win::App app;
  return app.Run();
}
