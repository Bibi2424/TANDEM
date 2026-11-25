#include "tandem/core_info.h"

#ifndef TANDEM_FW_VERSION
#define TANDEM_FW_VERSION "0.0.0-unknown"
#endif

#ifndef TANDEM_GIT_COMMIT
#define TANDEM_GIT_COMMIT "unknown"
#endif

namespace tandem {

const char* core_version() noexcept {
  return TANDEM_FW_VERSION;
}

const char* core_git_commit() noexcept {
  return TANDEM_GIT_COMMIT;
}

}  // namespace tandem
