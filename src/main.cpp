// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "app/application.h"

#ifdef KREMA_TEST_HOOKS
#include "frameprobe.h"
#endif

int main(int argc, char *argv[])
{
#ifdef KREMA_TEST_HOOKS
    // CI-only: inert unless KREMA_PROBE_NDJSON is set. The frame probe's
    // event dispatcher and animation driver must exist before QApplication.
    // See tests/ci/README.md.
    krema::testing::FrameProbe::installBeforeApplicationIfEnabled();
#endif
    krema::Application app(argc, argv);
    return app.run();
}
