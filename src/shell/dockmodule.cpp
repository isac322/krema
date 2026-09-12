// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dockmodule.h"

namespace krema
{

DockModule::DockModule(QObject *parent)
    : QObject(parent)
{
}

DockModule::~DockModule() = default;

} // namespace krema
