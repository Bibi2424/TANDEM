import re
import subprocess

Import("env")


def resolve_git_commit(project_dir: str) -> str:
    try:
        out = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=project_dir,
            stderr=subprocess.DEVNULL,
            timeout=3,
        )
        sha = out.decode("utf-8", errors="ignore").strip()
        if sha and re.fullmatch(r"[0-9a-fA-F]{4,16}", sha):
            return sha
    except Exception:
        pass
    return "unknown"


commit = resolve_git_commit(env.subst("$PROJECT_DIR"))
env.Append(CPPDEFINES=[("TANDEM_GIT_COMMIT", env.StringifyMacro(commit))])
