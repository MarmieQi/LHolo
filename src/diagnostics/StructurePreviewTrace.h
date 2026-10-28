#pragma once
#include <string_view>
namespace lholo::diagnostics {
// Synchronous breadcrumb journal: ordinary Logger messages are queued and
// may not reach the file before an access violation terminates the process.
void traceStructurePreview(std::string_view message) noexcept;
}
