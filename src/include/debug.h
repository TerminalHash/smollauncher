#ifndef DEBUG_H
#define DEBUG_H

#include <QString>

namespace Debug {

// Detailed logging requested with SL_DEBUG=1.
bool enabled();

// Normal events (startup, list load/save, imports, launches) are always
// written to <config>/Logs/smollauncher.log and stderr.
void log(const QString &message);

// Detailed diagnostics (selections, field dumps, scanning internals) are
// only written when SL_DEBUG=1.
void detail(const QString &message);

} // namespace Debug

#endif // DEBUG_H
