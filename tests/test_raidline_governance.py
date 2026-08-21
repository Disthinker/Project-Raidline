from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any, ClassVar

REPO_ROOT = Path(__file__).resolve().parents[1]
SCRIPT_PATH = REPO_ROOT / "tools/raidline_governance.py"
SPEC = importlib.util.spec_from_file_location("raidline_governance", SCRIPT_PATH)
assert SPEC is not None and SPEC.loader is not None
governance = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = governance
SPEC.loader.exec_module(governance)


class ContextRoutingTest(unittest.TestCase):
    context_map: ClassVar[dict[str, Any]]

    @classmethod
    def setUpClass(cls) -> None:
        cls.context_map = governance.load_context_map()

    def test_combat_task_loads_only_primary_context_initially(self) -> None:
        route = governance.route_task("修复准星后坐力", [], self.context_map)

        self.assertEqual(route["primary_domains"], ["combat"])
        self.assertEqual(route["context_files"], ["doc/context/domains/combat.md"])
        self.assertIn("session-persistence", route["adjacent_on_demand"])

    def test_changed_save_path_routes_to_persistence(self) -> None:
        route = governance.route_task(
            "adjust serialization", ["src/save_repository.cpp"], self.context_map
        )

        self.assertEqual(route["primary_domains"], ["session-persistence"])
        self.assertIn("area-persistence", route["test_labels"])

    def test_unknown_task_falls_back_to_delivery_context(self) -> None:
        route = governance.route_task(
            "unclassified local cleanup", [], self.context_map
        )

        self.assertEqual(route["primary_domains"], ["build-delivery"])


class EnvelopeTest(unittest.TestCase):
    context_map: ClassVar[dict[str, Any]]

    @classmethod
    def setUpClass(cls) -> None:
        cls.context_map = governance.load_context_map()

    def test_repository_envelope_is_valid(self) -> None:
        envelope = governance.load_envelope(
            REPO_ROOT / "doc/exec-plans/active/complexity-control.task.toml",
            self.context_map,
        )

        self.assertEqual(envelope["risk"], "cross-domain")

    def test_high_risk_envelope_requires_exec_plan(self) -> None:
        template = (
            REPO_ROOT / "doc/exec-plans/active/complexity-control.task.toml"
        ).read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "invalid.task.toml"
            path.write_text(
                template.replace(
                    'exec_plan = "doc/exec-plans/active/complexity-control.md"',
                    'exec_plan = ""',
                ),
                encoding="utf-8",
            )

            with self.assertRaises(governance.GovernanceError):
                governance.load_envelope(path, self.context_map)


class ArchitectureAndImpactTest(unittest.TestCase):
    context_map: ClassVar[dict[str, Any]]
    architecture: ClassVar[Any]

    @classmethod
    def setUpClass(cls) -> None:
        cls.context_map = governance.load_context_map()
        cls.architecture = governance.architecture_check()

    def test_current_production_architecture_passes(self) -> None:
        self.assertTrue(self.architecture.ok, self.architecture.errors)
        self.assertEqual(set(self.architecture.modules), set(governance.MODULE_ORDER))

    def test_stable_and_active_document_ownership_passes(self) -> None:
        self.assertEqual(governance.documentation_check(), ())

    def test_cmake_parser_finds_every_non_main_cpp_once(self) -> None:
        owned = [
            path
            for paths in self.architecture.modules.values()
            for path in paths
            if path.endswith(".cpp")
        ]
        expected = [
            path
            for path in (REPO_ROOT / "src").glob("*.cpp")
            if path.name != "main.cpp"
        ]

        self.assertEqual(len(owned), len(set(owned)))
        self.assertEqual(len(owned), len(expected))

    def test_authority_path_requires_authority_risk(self) -> None:
        envelope = governance.load_envelope(
            REPO_ROOT / "doc/exec-plans/active/complexity-control.task.toml",
            self.context_map,
        )
        report = governance.impact_report(
            ["src/save_repository.cpp"], self.context_map, self.architecture, envelope
        )

        self.assertTrue(any("authority" in error for error in report["errors"]))


if __name__ == "__main__":
    unittest.main()
