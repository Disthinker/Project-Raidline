#!/usr/bin/env python3
"""Small, dependency-free governance entry point for Project Raidline."""

from __future__ import annotations

import argparse
import fnmatch
import json
import os
import re
import subprocess
import sys
import tomllib
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Sequence, cast

REPO_ROOT = Path(__file__).resolve().parents[1]
CONTEXT_MAP_PATH = REPO_ROOT / "doc/context/context-map.json"
RISK_ORDER = {"local": 0, "domain": 1, "cross-domain": 2, "authority": 3}
MODULE_ORDER = {
    "raidline_domain": 0,
    "raidline_simulation": 1,
    "raidline_services": 2,
    "raidline_sdl_client": 3,
}
MODULE_ALIAS = {
    "domain": "raidline_domain",
    "simulation": "raidline_simulation",
    "services": "raidline_services",
    "sdl_client": "raidline_sdl_client",
}
REQUIRED_ENVELOPE_KEYS = {
    "schema_version",
    "task",
    "risk",
    "primary_domain",
    "secondary_domains",
    "allowed_paths",
    "read_only_context",
    "protected_domains",
    "relevant_invariants",
    "persistence_impact",
    "content_impact",
    "ownership_impact",
    "player_visible_behavior",
    "required_focused_labels",
    "required_integration_labels",
    "manual_acceptance",
    "explicit_exclusions",
    "exec_plan",
}


class GovernanceError(RuntimeError):
    pass


@dataclass(frozen=True)
class ArchitectureResult:
    errors: tuple[str, ...]
    modules: dict[str, tuple[str, ...]]

    @property
    def ok(self) -> bool:
        return not self.errors


def documentation_check(repo: Path = REPO_ROOT) -> tuple[str, ...]:
    errors: list[str] = []
    current_state = (repo / "doc/project/CURRENT_STATE.md").read_text(encoding="utf-8")
    forbidden_state_patterns = {
        r"(?m)^- .*当前开发分支": "CURRENT_STATE contains a current development branch",
        r"(?m)^- .*Draft PR #": "CURRENT_STATE contains transient Draft PR status",
        r"(?m)^- .*等待.*(?:CI|验收)": "CURRENT_STATE contains pending CI/acceptance",
        r"(?m)^当前活动计划：": "CURRENT_STATE points at a transient active plan",
    }
    for pattern, message in forbidden_state_patterns.items():
        if re.search(pattern, current_state):
            errors.append(message)

    for path in (repo / "doc/exec-plans/active").glob("*.md"):
        text = path.read_text(encoding="utf-8")
        if re.search(r"(?m)^状态：(?:已完成|完成|已接受)\s*$", text):
            relative = normalize_path(path.relative_to(repo))
            errors.append(f"completed plan remains active: {relative}")
    return tuple(sorted(errors))


def _run(
    command: Sequence[str],
    *,
    cwd: Path = REPO_ROOT,
    check: bool = True,
    capture: bool = True,
    env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        list(command),
        cwd=cwd,
        check=False,
        capture_output=capture,
        text=True,
        encoding="utf-8",
        errors="replace",
        env=env,
    )
    if check and result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        raise GovernanceError(
            f"command failed ({result.returncode}): {' '.join(command)}\n{detail}"
        )
    return result


def _git(*args: str, check: bool = True) -> str:
    return _run(("git", *args), check=check).stdout.strip()


def load_context_map(path: Path = CONTEXT_MAP_PATH) -> dict[str, Any]:
    raw = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict):
        raise GovernanceError(f"context map must be an object: {path}")
    data = cast(dict[str, Any], raw)
    if data.get("schema_version") != 1 or not isinstance(data.get("domains"), dict):
        raise GovernanceError(f"unsupported context map: {path}")
    return data


def load_envelope(path: Path, context_map: dict[str, Any]) -> dict[str, Any]:
    data = tomllib.loads(path.read_text(encoding="utf-8"))
    missing = sorted(REQUIRED_ENVELOPE_KEYS - data.keys())
    if missing:
        raise GovernanceError(f"task envelope is missing fields: {', '.join(missing)}")
    if data["schema_version"] != 1:
        raise GovernanceError("task envelope schema_version must be 1")
    if data["risk"] not in RISK_ORDER:
        raise GovernanceError(f"unknown risk level: {data['risk']}")
    known_domains = set(context_map["domains"])
    declared_domains = {data["primary_domain"], *data["secondary_domains"]}
    unknown = sorted(declared_domains - known_domains)
    if unknown:
        raise GovernanceError(
            f"task envelope contains unknown domains: {', '.join(unknown)}"
        )
    if data["risk"] in {"cross-domain", "authority"} and not data["exec_plan"]:
        raise GovernanceError(f"{data['risk']} tasks require an ExecPlan")
    return data


def normalize_path(value: str | Path) -> str:
    return str(value).replace("\\", "/").removeprefix("./")


def path_matches(path: str, patterns: Iterable[str]) -> bool:
    normalized = normalize_path(path)
    return any(
        fnmatch.fnmatchcase(normalized.lower(), pattern.lower()) for pattern in patterns
    )


def changed_files(base: str) -> list[str]:
    candidates: set[str] = set()
    commands = (
        ("diff", "--name-only", "--diff-filter=ACMRD", f"{base}...HEAD"),
        ("diff", "--name-only", "--diff-filter=ACMRD"),
        ("diff", "--cached", "--name-only", "--diff-filter=ACMRD"),
        ("ls-files", "--others", "--exclude-standard"),
    )
    for command in commands:
        output = _git(*command, check=False)
        candidates.update(
            normalize_path(line) for line in output.splitlines() if line.strip()
        )
    return sorted(candidates)


def route_task(
    task: str,
    paths: Iterable[str],
    context_map: dict[str, Any],
) -> dict[str, Any]:
    scores: Counter[str] = Counter()
    lowered = task.lower()
    normalized_paths = [normalize_path(path) for path in paths]
    for name, domain in context_map["domains"].items():
        for keyword in domain["keywords"]:
            if keyword.lower() in lowered:
                scores[name] += 1
        for path in normalized_paths:
            if path_matches(path, domain["paths"]):
                scores[name] += 4

    if scores:
        ordered = sorted(scores, key=lambda name: (-scores[name], name))
        primary = [ordered[0]]
        secondary = ordered[1:]
    else:
        primary = ["build-delivery"]
        secondary = []

    adjacent: set[str] = set()
    for name in primary:
        adjacent.update(context_map["domains"][name]["adjacent"])
    adjacent.difference_update(primary)
    adjacent.difference_update(secondary)
    return {
        "primary_domains": primary,
        "secondary_domains": secondary,
        "adjacent_on_demand": sorted(adjacent),
        "context_files": [context_map["domains"][name]["context"] for name in primary],
        "test_labels": sorted(
            {
                label
                for name in (*primary, *secondary)
                for label in context_map["domains"][name]["test_labels"]
            }
        ),
        "scores": dict(sorted(scores.items())),
    }


def parse_production_modules(cmake_text: str) -> dict[str, tuple[str, ...]]:
    modules: dict[str, tuple[str, ...]] = {}
    pattern = re.compile(
        r"add_library\((raidline_(?:domain|simulation|services|sdl_client))\s+STATIC(?P<body>.*?)\n\)",
        re.DOTALL,
    )
    for match in pattern.finditer(cmake_text):
        sources = tuple(
            re.findall(
                r"src/[A-Za-z0-9_./-]+\.(?:h|hpp|cpp|cc|cxx)", match.group("body")
            )
        )
        modules[match.group(1)] = sources
    return modules


def architecture_check(repo: Path = REPO_ROOT) -> ArchitectureResult:
    cmake_text = (repo / "CMakeLists.txt").read_text(encoding="utf-8")
    modules = parse_production_modules(cmake_text)
    errors: list[str] = []
    if set(modules) != set(MODULE_ORDER):
        errors.append(f"expected four production modules, found: {sorted(modules)}")

    owners_by_path: dict[str, str] = {}
    owners_by_name: dict[str, str] = {}
    for module, sources in modules.items():
        for source in sources:
            if source in owners_by_path:
                errors.append(f"production source has two owners: {source}")
            owners_by_path[source] = module
            name = Path(source).name
            if name in owners_by_name and owners_by_name[name] != module:
                errors.append(
                    f"duplicate production basename prevents include routing: {name}"
                )
            owners_by_name[name] = module

    expected_cpp = {
        normalize_path(path.relative_to(repo))
        for path in (repo / "src").glob("*.cpp")
        if path.name != "main.cpp"
    }
    owned_cpp = {path for path in owners_by_path if Path(path).suffix == ".cpp"}
    for path in sorted(expected_cpp - owned_cpp):
        errors.append(f"production cpp has no module owner: {path}")
    for path in sorted(owned_cpp - expected_cpp):
        errors.append(f"module owns unexpected cpp: {path}")

    include_pattern = re.compile(r"#[ \t]*include[ \t]*\"(?P<include>[^\"]+)\"")
    sdl_pattern = re.compile(r"#[ \t]*include[ \t]*[<\"]SDL")
    projectile_pattern = re.compile(
        r"\b(?:class|struct)\s+Projectile\b|\bstd::vector\s*<\s*Projectile\b"
    )
    for source, module in owners_by_path.items():
        source_path = repo / source
        text = source_path.read_text(encoding="utf-8")
        if module in {"raidline_domain", "raidline_simulation"} and sdl_pattern.search(
            text
        ):
            errors.append(f"{module} includes SDL: {source}")
        if projectile_pattern.search(text) or source_path.name.lower().startswith(
            "projectile."
        ):
            errors.append(f"production Projectile authority is forbidden: {source}")
        for include in include_pattern.finditer(text):
            included_owner = owners_by_name.get(Path(include.group("include")).name)
            if included_owner and MODULE_ORDER[included_owner] > MODULE_ORDER[module]:
                include_name = include.group("include")
                errors.append(
                    f"upward include: {module} {source} -> "
                    f"{included_owner} {include_name}"
                )

    link_pattern = re.compile(
        r"target_link_libraries\(\s*(raidline_(?:domain|simulation|services|sdl_client))(?P<body>.*?)\n\)",
        re.DOTALL,
    )
    for match in link_pattern.finditer(cmake_text):
        module = match.group(1)
        for alias in re.findall(
            r"raidline::(domain|simulation|services|sdl_client)", match.group("body")
        ):
            dependency = MODULE_ALIAS[alias]
            if MODULE_ORDER[dependency] > MODULE_ORDER[module]:
                errors.append(f"upward target dependency: {module} -> {dependency}")

    return ArchitectureResult(tuple(sorted(set(errors))), modules)


def _content_and_schema(repo: Path) -> tuple[str, int]:
    content = json.loads(
        (repo / "assets/content/v1/core.json").read_text(encoding="utf-8")
    )
    header = (repo / "src/save_repository.h").read_text(encoding="utf-8")
    match = re.search(r"schemaVersion\s*=\s*(\d+)", header)
    if not match:
        raise GovernanceError("could not derive current save schema")
    return str(content["content_version"]), int(match.group(1))


def _ctest_snapshot(repo: Path, build_dir: str) -> dict[str, Any]:
    path = repo / build_dir
    if not (path / "CTestTestfile.cmake").exists():
        return {"configured": False, "count": None, "labels": {}}
    result = _run(
        ("ctest", "--test-dir", str(path), "--show-only=json-v1"), check=False
    )
    if result.returncode != 0:
        return {
            "configured": True,
            "count": None,
            "labels": {},
            "error": result.stderr.strip(),
        }
    data = json.loads(result.stdout)
    labels: Counter[str] = Counter()
    for test in data.get("tests", []):
        for prop in test.get("properties", []):
            if prop.get("name") == "LABELS":
                labels.update(prop.get("value", []))
    return {
        "configured": True,
        "count": len(data.get("tests", [])),
        "labels": dict(sorted(labels.items())),
    }


def project_snapshot(
    repo: Path, context_map: dict[str, Any], build_dir: str
) -> dict[str, Any]:
    content_version, save_schema = _content_and_schema(repo)
    architecture = architecture_check(repo)
    documentation_errors = documentation_check(repo)
    status_lines = _git(
        "status", "--porcelain=v1", "--untracked-files=all"
    ).splitlines()
    branch_diff = _git("diff", "--name-only", "origin/main...HEAD").splitlines()
    active_dir = repo / "doc/exec-plans/active"
    active = sorted(
        normalize_path(path.relative_to(repo)) for path in active_dir.glob("*.*")
    )
    return {
        "branch": _git("branch", "--show-current"),
        "head": _git("rev-parse", "HEAD"),
        "origin_main": _git("rev-parse", "origin/main"),
        "worktree_clean": not status_lines,
        "worktree_changed_file_count": len(status_lines),
        "branch_diff_file_count": len(branch_diff),
        "production_modules": {
            name: len(paths) for name, paths in architecture.modules.items()
        },
        "content_version": content_version,
        "save_schema": save_schema,
        "ctest": _ctest_snapshot(repo, build_dir),
        "architecture_guard": "pass" if architecture.ok else "fail",
        "architecture_errors": list(architecture.errors),
        "documentation_guard": "pass" if not documentation_errors else "fail",
        "documentation_errors": list(documentation_errors),
        "active_exec_plans": active,
        "stable_contracts": context_map["stable_contracts"],
    }


def print_snapshot(snapshot: dict[str, Any]) -> None:
    print("Project Raidline snapshot")
    print(f"  branch: {snapshot['branch']}")
    print(f"  HEAD: {snapshot['head']}")
    print(f"  origin/main: {snapshot['origin_main']}")
    print(f"  worktree clean: {snapshot['worktree_clean']}")
    content_version = snapshot["content_version"]
    save_schema = snapshot["save_schema"]
    print(f"  content/save: {content_version} / schema v{save_schema}")
    ctest = snapshot["ctest"]
    count = ctest.get("count") if ctest.get("configured") else "not configured"
    print(f"  registered CTest: {count}")
    print(f"  architecture guard: {snapshot['architecture_guard']}")
    print(f"  documentation guard: {snapshot['documentation_guard']}")
    print(
        "  modules: "
        + ", ".join(
            f"{name}={count}" for name, count in snapshot["production_modules"].items()
        )
    )
    print("  active contracts:")
    for path in snapshot["active_exec_plans"]:
        print(f"    - {path}")


def _domains_for_paths(paths: Iterable[str], context_map: dict[str, Any]) -> list[str]:
    return sorted(
        name
        for name, domain in context_map["domains"].items()
        if any(path_matches(path, domain["paths"]) for path in paths)
    )


def impact_report(
    paths: Sequence[str],
    context_map: dict[str, Any],
    architecture: ArchitectureResult,
    envelope: dict[str, Any] | None,
) -> dict[str, Any]:
    owners = {
        source: module
        for module, sources in architecture.modules.items()
        for source in sources
    }
    modules = sorted({owners[path] for path in paths if path in owners})
    domains = _domains_for_paths(paths, context_map)
    categories = {
        "production": [path for path in paths if path.startswith(("src/", "ports/"))],
        "tests": [path for path in paths if path.startswith("tests/")],
        "docs": [
            path
            for path in paths
            if path.startswith(("doc/", ".agents/", ".codex/")) or path == "AGENTS.md"
        ],
        "build_ci": [
            path
            for path in paths
            if path in {"CMakeLists.txt", "CMakePresets.json"}
            or path.startswith(
                ("cmake/", ".github/workflows/", "tools/raidline_governance.py")
            )
        ],
        "content": [path for path in paths if path.startswith("assets/content/")],
        "persistence": [
            path
            for path in paths
            if path_matches(
                path,
                (
                    "src/profile_state.*",
                    "src/save_repository.*",
                    "src/raid_lifecycle.*",
                    "src/raid_settlement.*",
                ),
            )
        ],
    }
    errors: list[str] = []
    warnings: list[str] = []
    if envelope:
        outside = [
            path for path in paths if not path_matches(path, envelope["allowed_paths"])
        ]
        if outside:
            errors.append("changed paths outside task envelope: " + ", ".join(outside))
        protected = sorted(set(domains) & set(envelope["protected_domains"]))
        if protected:
            errors.append("protected domains changed: " + ", ".join(protected))
        declared = {envelope["primary_domain"], *envelope["secondary_domains"]}
        undeclared = sorted(set(domains) - declared - {"build-delivery"})
        if undeclared:
            errors.append(
                "changed domains missing from envelope: " + ", ".join(undeclared)
            )
        if any(path_matches(path, context_map["authority_paths"]) for path in paths):
            if envelope["risk"] != "authority":
                errors.append("authority paths require risk=authority")

        soft_limits = {
            "local": (5, 1),
            "domain": (15, 1),
            "cross-domain": (35, 3),
            "authority": (60, 4),
        }
        production_limit, domain_limit = soft_limits[envelope["risk"]]
        if (
            len(categories["production"]) > production_limit
            or len(domains) > domain_limit
        ):
            warnings.append(
                "blast radius exceeds the soft envelope; record why the "
                "player/engineering outcome needs this scope or split the slice"
            )
    stable_docs = {
        "doc/project/CURRENT_STATE.md",
        "doc/project/KNOWN_ISSUES.md",
        "doc/project/ROADMAP.md",
    }
    if stable_docs.issubset(categories["docs"]):
        warnings.append(
            "stable project documents changed together; verify this is "
            "accepted-state/route governance, not transient PR bookkeeping"
        )
    return {
        "files": list(paths),
        "counts": {name: len(value) for name, value in categories.items()},
        "domains": domains,
        "modules": modules,
        "persistence_touched": bool(categories["persistence"]),
        "content_touched": bool(categories["content"]),
        "errors": errors,
        "warnings": warnings,
    }


def print_impact(report: dict[str, Any]) -> None:
    print("Change impact")
    print(f"  files: {len(report['files'])}")
    print(
        "  counts: "
        + ", ".join(f"{name}={count}" for name, count in report["counts"].items())
    )
    print(f"  domains: {', '.join(report['domains']) or 'none'}")
    print(f"  modules: {', '.join(report['modules']) or 'none'}")
    for warning in report["warnings"]:
        print(f"  WARNING: {warning}")
    for error in report["errors"]:
        print(f"  ERROR: {error}")


def _test_environment(build_dir: Path) -> dict[str, str]:
    env = os.environ.copy()
    dll_dir = build_dir / "vcpkg_installed/x64-windows/debug/bin"
    if dll_dir.exists():
        env["PATH"] = str(dll_dir) + os.pathsep + env.get("PATH", "")
    return env


def run_ctest(build_dir: Path, label: str | None = None) -> None:
    command = ["ctest", "--test-dir", str(build_dir), "--output-on-failure", "-j", "8"]
    if label:
        command.extend(("-L", label))
    print("RUN " + " ".join(command), flush=True)
    result = _run(command, capture=False, env=_test_environment(build_dir))
    if result.returncode != 0:
        raise GovernanceError(f"CTest failed for label: {label or 'full'}")


def _print_route(route: dict[str, Any]) -> None:
    print("Context route")
    print("  primary: " + ", ".join(route["primary_domains"]))
    print("  load now:")
    for path in route["context_files"]:
        print(f"    - {path}")
    print(
        "  adjacent only if evidence requires it: "
        + (", ".join(route["adjacent_on_demand"]) or "none")
    )
    print("  focused labels: " + (", ".join(route["test_labels"]) or "none"))


def command_snapshot(args: argparse.Namespace, context_map: dict[str, Any]) -> int:
    snapshot = project_snapshot(REPO_ROOT, context_map, args.build_dir)
    if args.format == "json":
        text = json.dumps(snapshot, ensure_ascii=False, indent=2) + "\n"
        print(text, end="")
    else:
        print_snapshot(snapshot)
        text = json.dumps(snapshot, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        output = REPO_ROOT / args.output
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(text, encoding="utf-8")
    return (
        0
        if snapshot["architecture_guard"] == "pass"
        and snapshot["documentation_guard"] == "pass"
        else 1
    )


def command_route(args: argparse.Namespace, context_map: dict[str, Any]) -> int:
    paths = args.changed_file or []
    if args.use_diff:
        paths = [*paths, *changed_files(args.base)]
    route = route_task(args.task, paths, context_map)
    if args.format == "json":
        print(json.dumps(route, ensure_ascii=False, indent=2))
    else:
        _print_route(route)
    return 0


def command_architecture(
    _args: argparse.Namespace, _context_map: dict[str, Any]
) -> int:
    result = architecture_check()
    print(f"Architecture guard: {'PASS' if result.ok else 'FAIL'}")
    print(
        "  modules: "
        + ", ".join(f"{name}={len(paths)}" for name, paths in result.modules.items())
    )
    for error in result.errors:
        print(f"  ERROR: {error}")
    return 0 if result.ok else 1


def command_documentation(
    _args: argparse.Namespace, _context_map: dict[str, Any]
) -> int:
    errors = documentation_check()
    print(f"Documentation guard: {'PASS' if not errors else 'FAIL'}")
    for error in errors:
        print(f"  ERROR: {error}")
    return 0 if not errors else 1


def command_impact(args: argparse.Namespace, context_map: dict[str, Any]) -> int:
    envelope = (
        load_envelope(REPO_ROOT / args.envelope, context_map) if args.envelope else None
    )
    architecture = architecture_check()
    paths = sorted({*changed_files(args.base), *(args.changed_file or [])})
    report = impact_report(paths, context_map, architecture, envelope)
    if args.format == "json":
        print(json.dumps(report, ensure_ascii=False, indent=2))
    else:
        print_impact(report)
    return 0 if architecture.ok and not report["errors"] else 1


def command_preflight(args: argparse.Namespace, context_map: dict[str, Any]) -> int:
    if args.fetch:
        print("RUN git fetch origin", flush=True)
        _run(("git", "fetch", "origin"), capture=False)
    envelope = (
        load_envelope(REPO_ROOT / args.envelope, context_map) if args.envelope else None
    )
    snapshot = project_snapshot(REPO_ROOT, context_map, args.build_dir)
    output = REPO_ROOT / "build/governance/project-snapshot.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(snapshot, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print_snapshot(snapshot)
    paths = changed_files(args.base)
    _print_route(route_task(args.task, paths, context_map))
    report = impact_report(paths, context_map, architecture_check(), envelope)
    print_impact(report)
    if args.run_sentinel:
        run_ctest(REPO_ROOT / args.build_dir, "sentinel")
    return (
        0
        if snapshot["architecture_guard"] == "pass"
        and snapshot["documentation_guard"] == "pass"
        and not report["errors"]
        else 1
    )


def command_postflight(args: argparse.Namespace, context_map: dict[str, Any]) -> int:
    envelope = load_envelope(REPO_ROOT / args.envelope, context_map)
    architecture = architecture_check()
    documentation_errors = documentation_check()
    report = impact_report(
        changed_files(args.base), context_map, architecture, envelope
    )
    print_impact(report)
    print(f"Architecture guard: {'PASS' if architecture.ok else 'FAIL'}")
    for error in architecture.errors:
        print(f"  ERROR: {error}")
    print(f"Documentation guard: {'PASS' if not documentation_errors else 'FAIL'}")
    for error in documentation_errors:
        print(f"  ERROR: {error}")
    diff_check = _run(("git", "diff", "--check"), check=False)
    if diff_check.stdout or diff_check.stderr:
        print(diff_check.stdout or diff_check.stderr)
    if args.run_tests and architecture.ok and not report["errors"]:
        build_dir = REPO_ROOT / args.build_dir
        labels = ["sentinel", *envelope["required_focused_labels"]]
        if envelope["risk"] in {"cross-domain", "authority"}:
            labels.append("layer-integration")
        labels.extend(envelope["required_integration_labels"])
        if envelope["risk"] == "authority":
            labels.extend(("area-persistence", "long-sequence"))
        for label in dict.fromkeys(labels):
            run_ctest(build_dir, label)
        run_ctest(build_dir)
    ok = (
        architecture.ok
        and not documentation_errors
        and not report["errors"]
        and diff_check.returncode == 0
    )
    return 0 if ok else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    snapshot = subparsers.add_parser(
        "snapshot", help="derive a lightweight project snapshot"
    )
    snapshot.add_argument("--build-dir", default="build/windows-debug")
    snapshot.add_argument("--format", choices=("text", "json"), default="text")
    snapshot.add_argument("--output")
    snapshot.set_defaults(handler=command_snapshot)

    route = subparsers.add_parser(
        "route", help="route a task to minimal domain context"
    )
    route.add_argument("--task", required=True)
    route.add_argument("--changed-file", action="append")
    route.add_argument("--use-diff", action="store_true")
    route.add_argument("--base", default="origin/main")
    route.add_argument("--format", choices=("text", "json"), default="text")
    route.set_defaults(handler=command_route)

    architecture = subparsers.add_parser(
        "architecture", help="verify module ownership and dependency direction"
    )
    architecture.set_defaults(handler=command_architecture)

    documentation = subparsers.add_parser(
        "documentation", help="verify stable/active document ownership"
    )
    documentation.set_defaults(handler=command_documentation)

    impact = subparsers.add_parser("impact", help="report actual task blast radius")
    impact.add_argument("--base", default="origin/main")
    impact.add_argument("--envelope")
    impact.add_argument("--changed-file", action="append")
    impact.add_argument("--format", choices=("text", "json"), default="text")
    impact.set_defaults(handler=command_impact)

    preflight = subparsers.add_parser(
        "preflight", help="run the standard task-start checks"
    )
    preflight.add_argument("--task", required=True)
    preflight.add_argument("--envelope")
    preflight.add_argument("--base", default="origin/main")
    preflight.add_argument("--build-dir", default="build/windows-debug")
    preflight.add_argument("--fetch", action="store_true")
    preflight.add_argument("--run-sentinel", action="store_true")
    preflight.set_defaults(handler=command_preflight)

    postflight = subparsers.add_parser(
        "postflight", help="run scope, architecture and evidence checks"
    )
    postflight.add_argument("--envelope", required=True)
    postflight.add_argument("--base", default="origin/main")
    postflight.add_argument("--build-dir", default="build/windows-debug")
    postflight.add_argument("--run-tests", action="store_true")
    postflight.set_defaults(handler=command_postflight)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    try:
        parser = build_parser()
        args = parser.parse_args(argv)
        return int(args.handler(args, load_context_map()))
    except (
        GovernanceError,
        OSError,
        json.JSONDecodeError,
        tomllib.TOMLDecodeError,
    ) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
