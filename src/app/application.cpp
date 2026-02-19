// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "application.h"

#include <LayerShellQt/Shell>

namespace krema
{

int Application::run()
{
    LayerShellQt::Shell::useLayerShell();
    return exec();
}

} // namespace krema
