// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QApplication>

namespace krema
{

class Application : public QApplication
{
    Q_OBJECT

public:
    using QApplication::QApplication;

    int run();
};

} // namespace krema
