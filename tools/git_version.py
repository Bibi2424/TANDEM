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


TARGET_SUFFIX = "lib/tandem_core/src/core_info.cpp"


def inject_git_commit(target_env, node):
    """Attach TANDEM_GIT_COMMIT only to core_info.cpp so commits do not bust build caches."""
    src_path = node.srcnode().get_path().replace("\\", "/")
    if src_path != TARGET_SUFFIX and not src_path.endswith("/" + TARGET_SUFFIX):
        return node

    node_env = target_env.Clone()
    node_env.ProcessUnFlags(node_env.get("BUILD_UNFLAGS"))
    node_env.Append(
        CPPDEFINES=[("TANDEM_GIT_COMMIT", node_env.StringifyMacro(commit))]
    )
    return node_env.Object(node)


commit = resolve_git_commit(env.subst("$PROJECT_DIR"))
env.AddBuildMiddleware(inject_git_commit, "*core_info.cpp")
