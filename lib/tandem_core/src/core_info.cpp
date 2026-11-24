#include "tandem/core_info.h"

#ifndef TANDEM_GIT_COMMIT
#define TANDEM_GIT_COMMIT "unknown"
#endif

namespace tandem {

const char* core_version() noexcept {
  return "0.1.0";
}

const char* core_git_commit() noexcept {
  return TANDEM_GIT_COMMIT;
}

}  // namespace tandem
