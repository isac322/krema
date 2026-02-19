// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "app/application.h"

int main(int argc, char *argv[])
{
    krema::Application app(argc, argv);
    return app.run();
}
