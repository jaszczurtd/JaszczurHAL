#!/usr/bin/env python3
"""The tools with a project's device_tree.json: assembly selection by the
rules every tool shares, the assembly x variant matrix, separate build
directories, the generated VS Code files, the library workspace and the
feature lint. Projects live in directories with spaces; one uses CRLF."""

from __future__ import annotations

from contextlib import redirect_stdout
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import unittest
from unittest import mock

from repo_root import repo_root  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT))

import device_tree_project  # noqa: E402
import examples_dispatcher  # noqa: E402
import generate_hal_features  # noqa: E402
import project_config  # noqa: E402
from vscode.runtime import jh_vscode  # noqa: E402
import vscode_library_workspace as library  # noqa: E402
import vscode_task_config  # noqa: E402

WORK = ROOT / ".build" / "tests" / "device tree tools"
ENTRY = ROOT / "vscode" / "entry" / ("jh-vscode.cmd" if os.name == "nt" else "jh-vscode")
CANHAT = ROOT / "tests" / "fixtures" / "device_tree" / "hal" / "nucleo-canhat" / "device_tree.json"
LIBRARY = ROOT / "tests" / "fixtures" / "device_tree" / "hal" / "library"
# The UART variant collides with the second assembly's relay pin (PC4 is 36):
# only the header evaluated with that assembly's jh_hardware.h can tell.
HEADER = """#pragma once
#define HAL_ENABLE_STM32G474_FDCAN
#define JH_PROJECT_VARIANTS(X) X(SERIAL, "Adds the UART", HAL_ENABLE_UART)
#if defined(HAL_ENABLE_UART) && JH_HW_NODE_HAT_RELAY1_PIN_OUT == 36U
#error "the UART collides with the relay on PC4"
#endif
"""
MANIFEST = {"project": "device tree tools", "module": "app", "toolchain": "cmake",
            "cmakeBuildDir": "${buildDir}/cmake",
            "cmake": {"sourceDir": "${jhRoot}/cmake/jh_firmware_project",
                      "cache": {"JH_PROJECT_DIR": "${project}", "JH_MODULE_NAME": "app"}}}


def project(name: str, manifest: dict | None = None, newline: str = "\n") -> Path:
    """Two assemblies of one target and board, canhat and canhat-b, which
    differ in one relay pin."""
    directory = WORK / name
    if directory.exists():
        shutil.rmtree(directory)
    (directory / ".vscode").mkdir(parents=True)
    data = json.loads(CANHAT.read_text(encoding="utf-8"))
    second = json.loads(json.dumps(data["assemblies"]["canhat"]))
    second["nodes"]["hat.relay1"]["pins"]["out"] = "PC4"
    data["assemblies"]["canhat-b"] = second
    (directory / "device_tree.json").write_bytes(json.dumps(data, indent=2).replace("\n", newline).encode())
    (directory / "hal_project_config.h").write_bytes(HEADER.replace("\n", newline).encode())
    (directory / "app.c").write_text("void app_start(void) {}\nvoid app_task0(void) {}\n", encoding="utf-8")
    write_json(directory / ".vscode" / "jaszczurhal.project.json", manifest or MANIFEST)
    return directory


def write_json(path: Path, data: dict) -> None:
    path.write_text(json.dumps(data, indent=4) + "\n", encoding="utf-8")


def labels(builds: list) -> list[str]:
    return sorted(f"{build.assembly}:{build.variant.id if build.variant else 'base'}" for build in builds)


def run(*arguments: str | Path) -> subprocess.CompletedProcess:
    return subprocess.run([str(ENTRY), *map(str, arguments)], capture_output=True, text=True)


class Selection(unittest.TestCase):
    def setUp(self) -> None:
        self.dir = project("selection")

    def choose(self, target=None, assembly=None, stored=()) -> str:
        return device_tree_project.choose(self.dir, target, assembly, stored)[0]["name"]

    def test_the_rules_of_one_and_several_assemblies(self) -> None:
        with self.assertRaisesRegex(ValueError, r"choose one of the assemblies \['canhat', 'canhat-b'\]"):
            self.choose()
        with self.assertRaisesRegex(ValueError, "choose one of the assemblies"):
            self.choose(target="stm32g474")
        with self.assertRaisesRegex(ValueError, "no assembly for target rp2040"):
            self.choose(target="rp2040")
        with self.assertRaisesRegex(ValueError, "unknown assembly 'other'"):
            self.choose(assembly="other")
        with self.assertRaisesRegex(ValueError, "is for stm32g474, not rp2040"):
            self.choose(target="rp2040", assembly="canhat")
        self.assertEqual("canhat-b", self.choose(assembly="canhat-b"))
        single = LIBRARY
        self.assertEqual("picow", device_tree_project.choose(single, "rp2040", None)[0]["name"])

    def test_explicit_then_local_then_manifest(self) -> None:
        stored = [("canhat-b", "local"), ("canhat", "manifest")]
        self.assertEqual("canhat", self.choose(assembly="canhat", stored=stored))
        self.assertEqual("canhat-b", self.choose(stored=stored))
        self.assertEqual("canhat", self.choose(stored=[(None, "local"), ("canhat", "manifest")]))
        # A stored key of another target is skipped under a target filter.
        self.assertEqual("picow", device_tree_project.choose(
            LIBRARY, "rp2040", None, [("pico2w", "local")])[0]["name"])
        with self.assertRaisesRegex(ValueError, "local selects assembly 'gone'"):
            self.choose(stored=[("gone", "local")])

    def test_a_broken_file_never_falls_back_to_the_board_path(self) -> None:
        (self.dir / "device_tree.json").write_text("{", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "JH-HW-SCHEMA"):
            device_tree_project.assemblies(self.dir)
        with self.assertRaisesRegex(ValueError, "JH-HW-SCHEMA"):
            jh_vscode.load_project_config(self.dir, use_local_state=False)


class Matrix(unittest.TestCase):
    def test_assembly_times_variant_from_the_files_alone(self) -> None:
        directory = project("matrix")
        # A stored selection never narrows the matrix.
        write_json(directory / ".vscode" / "jaszczurhal.local.json", {"assembly": "canhat"})
        builds = project_config.available_builds(directory, project_config.load_targets(), [""])
        self.assertEqual(["canhat-b:base", "canhat:SERIAL", "canhat:base"], labels(builds))
        self.assertTrue(all(build.target.id == "stm32g474" for build in builds))
        self.assertEqual(labels(builds), labels(jh_vscode.project_builds(
            directory, jh_vscode.load_json_file(directory / ".vscode" / "jaszczurhal.project.json"))))

    def test_crlf_files_read_like_lf(self) -> None:
        builds = project_config.available_builds(project("crlf", newline="\r\n"),
                                                  project_config.load_targets(), [""])
        self.assertEqual(["canhat-b:base", "canhat:SERIAL", "canhat:base"], labels(builds))

    def test_targets_come_only_from_the_assemblies(self) -> None:
        directory = project("targets")
        header = directory / "hal_project_config.h"
        header.write_text(header.read_text(encoding="utf-8")
                          + "#define JH_PROJECT_TARGETS(X) X(HAL_TARGET_STM32G474)\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "JH-HW-TARGETS"):
            project_config.available_builds(directory, project_config.load_targets(), [""])

    def test_the_library_build_key_has_one_source(self) -> None:
        key = device_tree_project.build_key(LIBRARY, "picow", ["variant=", "all=1"])
        self.assertRegex(key, r"^picow/[0-9a-f]{16}$")
        self.assertEqual(key, device_tree_project.build_key(LIBRARY, "picow", ["all=1", "variant="]))
        self.assertNotEqual(key, device_tree_project.build_key(LIBRARY, "picow", ["variant=X", "all=1"]))
        self.assertEqual("auto/", device_tree_project.build_key(LIBRARY, "", [])[:5])
        shell = subprocess.run(
            ["bash", "-c", f'source "{ROOT}/scripts/lib/build_artifacts.sh"; '
             f'jh_device_tree_build_key "{LIBRARY}" picow variant= all=1'],
            capture_output=True, text=True, check=True)
        self.assertEqual(key, shell.stdout.strip())


class Workflow(unittest.TestCase):
    def setUp(self) -> None:
        self.dir = project("workflow")
        self.local = self.dir / ".vscode" / "jaszczurhal.local.json"

    def config(self, **options) -> dict:
        return jh_vscode.load_project_config(self.dir, **options)

    def test_explicit_then_local_then_manifest(self) -> None:
        manifest = {**MANIFEST, "assembly": "canhat"}
        write_json(self.dir / ".vscode" / "jaszczurhal.project.json", manifest)
        self.assertEqual("canhat", self.config()["assembly"])
        write_json(self.local, {"assembly": "canhat-b"})
        self.assertEqual("canhat-b", self.config()["assembly"])
        self.assertEqual("canhat", self.config(assembly_override="canhat")["assembly"])
        self.assertEqual("canhat", self.config(use_local_state=False)["assembly"])

    def test_the_assembly_replaces_the_board_in_cmake(self) -> None:
        config = self.config(assembly_override="canhat-b")
        self.assertEqual(("stm32g474", "nucleo-g474re"), (config["target"], config["board"]))
        cache = config["cmake"]["cache"]
        self.assertEqual("canhat-b", cache["JH_ASSEMBLY"])
        self.assertNotIn("JH_BOARD", cache)
        # The assembly has its own buildDir; everything derived from it follows.
        self.assertEqual(("stm32g474", "canhat-b"), Path(config["buildDir"]).parts[-2:])
        self.assertEqual(config["buildDir"], cache["JH_ARTIFACT_DIR"])
        first = jh_vscode.get_cmake_build_dir(config, self.dir)
        self.assertEqual(("stm32g474", "canhat-b", "cmake"), first.parts[-3:])
        other = self.config(assembly_override="canhat")
        self.assertNotEqual(first, jh_vscode.get_cmake_build_dir(other, self.dir))
        self.assertNotEqual(config["buildDir"], other["buildDir"])
        jh_vscode.apply_variant(config, "SERIAL")
        self.assertEqual(("stm32g474", "canhat-b", "cmake", "variants", "SERIAL"),
                         jh_vscode.get_cmake_build_dir(config, self.dir).parts[-5:])
        self.assertEqual(("stm32g474", "canhat-b", "variants", "SERIAL"),
                         Path(config["cmake"]["cache"]["JH_ARTIFACT_DIR"]).parts[-4:])

    def test_derived_paths_follow_the_assembly(self) -> None:
        manifest = {**MANIFEST, "cmakeBuildDir": "${project}/cmake trees",
                    "artifacts": {"elf": "${buildDir}/firmware.elf"},
                    "cmake": {**MANIFEST["cmake"], "cache": {
                        **MANIFEST["cmake"]["cache"], "JH_ARTIFACT_DIR": "${project}/published"}}}
        write_json(self.dir / ".vscode" / "jaszczurhal.project.json", manifest)
        configs = [self.config(assembly_override=name) for name in ("canhat", "canhat-b")]
        for key in (lambda c: c["artifacts"]["elf"], lambda c: c["cmake"]["cache"]["JH_ARTIFACT_DIR"],
                    lambda c: jh_vscode.get_cmake_build_dir(c, self.dir)):
            self.assertNotEqual(key(configs[0]), key(configs[1]))
        self.assertEqual(("published", "stm32g474", "canhat"),
                         Path(configs[0]["cmake"]["cache"]["JH_ARTIFACT_DIR"]).parts[-3:])
        # A cmakeBuildDir outside buildDir still gets <target>/<assembly>.
        self.assertEqual(("cmake trees", "stm32g474", "canhat"),
                         jh_vscode.get_cmake_build_dir(configs[0], self.dir).parts[-3:])

    def test_board_selectors_are_refused(self) -> None:
        with self.assertRaisesRegex(ValueError, r"JH-HW-BOARD\] --board"):
            self.config(board_override="nucleo-g474re")
        for field in ({"board": "nucleo-g474re"}, {"target": "stm32g474"},
                      {"targetProfiles": {"stm32g474": {"board": "nucleo-g474re"}}},
                      {"cmake": {**MANIFEST["cmake"], "cache": {"JH_BOARD": "nucleo-g474re"}}}):
            with self.subTest(field=field):
                write_json(self.dir / ".vscode" / "jaszczurhal.project.json", {**MANIFEST, **field})
                with self.assertRaisesRegex(ValueError, "JH-HW-BOARD"):
                    self.config(assembly_override="canhat")

    def test_a_stale_local_board_stops_only_builds_that_use_it(self) -> None:
        write_json(self.local, {"target": "stm32g474", "board": "nucleo-g474re"})
        with self.assertRaisesRegex(ValueError, "was selected for the board path"):
            self.config()
        self.assertEqual("canhat", self.config(assembly_override="canhat")["assembly"])

    def test_an_assembly_needs_the_device_tree(self) -> None:
        (self.dir / "device_tree.json").unlink()
        with self.assertRaisesRegex(ValueError, r"JH-HW-ASSEMBLY\] --assembly"):
            self.config(assembly_override="canhat")
        write_json(self.dir / ".vscode" / "jaszczurhal.project.json", {**MANIFEST, "assembly": "canhat"})
        with self.assertRaisesRegex(ValueError, "JH-HW-ASSEMBLY"):
            self.config()
        # The tool's own record of the earlier device tree is dropped.
        write_json(self.dir / ".vscode" / "jaszczurhal.project.json", MANIFEST)
        write_json(self.local, {"assembly": "canhat"})
        self.assertNotIn("assembly", self.config())

    def test_the_configuration_is_read_with_the_assembly(self) -> None:
        config = self.config(assembly_override="canhat-b")
        jh_vscode.apply_variant(config, "SERIAL")
        with self.assertRaisesRegex(ValueError, "collides with the relay"):
            jh_vscode.project_build(config, self.dir)
        config = self.config(assembly_override="canhat")
        jh_vscode.apply_variant(config, "SERIAL")
        self.assertIn("HAL_ENABLE_UART", jh_vscode.project_build(config, self.dir).requested_features())

    def test_an_esp_assembly_builds_in_its_own_tree(self) -> None:
        directory = WORK / "esp project"
        if directory.exists():
            shutil.rmtree(directory)
        shutil.copytree(LIBRARY, directory)
        (directory / ".vscode").mkdir()
        write_json(directory / ".vscode" / "jaszczurhal.project.json",
                   {"project": "esp", "module": "app", "toolchain": "cmake"})
        config = jh_vscode.load_project_config(directory, assembly_override="esp32s3-zero",
                                               use_local_state=False)
        self.assertEqual(("esp32s3", "esp32s3-zero"), Path(config["buildDir"]).parts[-2:])
        self.assertTrue(config["espIdf"]["artifactManifest"].startswith(config["buildDir"]))
        command = jh_vscode.esp_idf_runner_command(config, directory, "build")
        self.assertEqual("esp32s3-zero", command[command.index("--assembly") + 1])
        self.assertNotIn("--board", command)

    def test_a_target_overlay_build_dir_is_the_base(self) -> None:
        directory = WORK / "esp overlay project"
        if directory.exists():
            shutil.rmtree(directory)
        shutil.copytree(LIBRARY, directory)
        data = json.loads((directory / "device_tree.json").read_text(encoding="utf-8"))
        data["assemblies"]["esp32s3-zero-b"] = data["assemblies"]["esp32s3-zero"]
        (directory / "device_tree.json").write_text(json.dumps(data, indent=2), encoding="utf-8")
        (directory / ".vscode").mkdir()
        write_json(directory / ".vscode" / "jaszczurhal.project.json",
                   {"project": "esp", "module": "app", "toolchain": "cmake",
                    "targetProfiles": {"esp32s3": {"buildDir": "${project}/esp out"}}})
        configs = [jh_vscode.load_project_config(directory, assembly_override=name, use_local_state=False)
                   for name in ("esp32s3-zero", "esp32s3-zero-b")]
        self.assertEqual(["esp32s3-zero", "esp32s3-zero-b"], [Path(c["buildDir"]).name for c in configs])
        self.assertTrue(all(Path(c["buildDir"]).parts[-3] == "esp out" for c in configs))
        self.assertNotEqual(*(c["espIdf"]["artifactManifest"] for c in configs))

    def test_native_windows_trees_are_kept_apart_by_assembly(self) -> None:
        state = WORK / "windows host environment.json"
        build_root = WORK / "windows build root"
        state.write_text(json.dumps({"schemaVersion": 1, "buildRoot": str(build_root), "tools": {}}),
                         encoding="utf-8")
        with mock.patch.dict(os.environ, {"JH_TEST_WINDOWS_HOST": "1",
                                          "JH_WINDOWS_HOST_ENVIRONMENT": str(state)}):
            trees = [jh_vscode.get_cmake_build_dir(self.config(assembly_override=name), self.dir)
                     for name in ("canhat", "canhat-b")]
        for tree, name in zip(trees, ("canhat", "canhat-b")):
            self.assertTrue(jh_vscode.path_within(tree, build_root))
            self.assertEqual(("cmake", "stm32g474", name), tree.parts[-3:])

    def test_a_backslash_in_the_project_path(self) -> None:
        directory = project("back\\slash")
        builds = project_config.available_builds(directory, project_config.load_targets(), [""])
        self.assertEqual(["canhat-b:base", "canhat:SERIAL", "canhat:base"], labels(builds))
        self.assertEqual("canhat", jh_vscode.load_project_config(
            directory, assembly_override="canhat", use_local_state=False)["assembly"])

    def test_select_board_stores_only_the_assembly(self) -> None:
        write_json(self.local, {"target": "stm32g474", "board": "nucleo-g474re", "uploadPort": "/dev/ttyACM0"})
        listing = run("select-board", "--project", self.dir, "--json")
        self.assertEqual(0, listing.returncode, listing.stderr)
        self.assertEqual(["canhat", "canhat-b"], [item["name"] for item in json.loads(listing.stdout)["assemblies"]])
        refused = run("select-board", "--project", self.dir, "--board", "nucleo-g474re")
        self.assertNotEqual(0, refused.returncode)
        self.assertIn("JH-HW-BOARD", refused.stderr)
        picked = run("select-board", "--project", self.dir, "--selection", "canhat-b - stm32g474/nucleo-g474re")
        self.assertEqual(0, picked.returncode, picked.stderr)
        self.assertEqual({"assembly": "canhat-b", "uploadPort": "/dev/ttyACM0"},
                         json.loads(self.local.read_text(encoding="utf-8")))
        unknown = run("select-board", "--project", self.dir, "--assembly", "other")
        self.assertNotEqual(0, unknown.returncode)
        self.assertEqual("canhat-b", json.loads(self.local.read_text(encoding="utf-8"))["assembly"])


class VsCodeFiles(unittest.TestCase):
    def test_the_picker_lists_the_assemblies(self) -> None:
        directory = project("picker", {**MANIFEST, "assembly": "canhat-b"})
        registry = jh_vscode.load_target_registry()
        document = vscode_task_config.project_tasks_document(
            registry, "stm32g474", "nucleo-g474re", project_dir=directory, selected_assembly="canhat-b")
        picker = document["inputs"][0]
        self.assertEqual(["canhat - stm32g474/nucleo-g474re", "canhat-b - stm32g474/nucleo-g474re"],
                         picker["options"])
        self.assertEqual("canhat-b - stm32g474/nucleo-g474re", picker["default"])
        tasks = {task["label"]: task for task in document["tasks"]}
        self.assertIn("assembly of device_tree.json", tasks["Project: Select board (GUI)"]["detail"])
        self.assertIn("Project: Build variant: SERIAL", tasks)
        # sync-board-picker writes the same picker, from the manifest only.
        write_json(directory / ".vscode" / "jaszczurhal.local.json", {"assembly": "canhat"})
        write_json(directory / ".vscode" / "tasks.json", {"version": "2.0.0", "tasks": []})
        result = run("sync-board-picker", "--project", directory)
        self.assertEqual(0, result.returncode, result.stderr)
        synced = json.loads((directory / ".vscode" / "tasks.json").read_text(encoding="utf-8"))
        self.assertEqual(picker, synced["inputs"][0])
        # The debugged ELF is the one the manifest's assembly publishes.
        launch = json.loads((directory / ".vscode" / "launch.json").read_text(encoding="utf-8"))
        executables = {item.get("executable") for item in launch["configurations"]}
        self.assertEqual({"${workspaceFolder}/.build/stm32g474/canhat-b/firmware.elf"}, executables)

    def test_cmake_tools_settings_name_the_assembly(self) -> None:
        registry = jh_vscode.load_target_registry()

        def settings(assembly, cache=None):
            return vscode_task_config.cmake_tools_configure_settings(
                registry, target="stm32g474", board="nucleo-g474re", module="app",
                jh_root_ref="${workspaceFolder}", build_ref="${workspaceFolder}/.build",
                project_cache=cache, assembly=assembly)

        first = settings("canhat")
        self.assertEqual("canhat", first["JH_ASSEMBLY"])
        self.assertNotIn("JH_BOARD", first)
        # CMake Tools publishes into the assembly's own directory, never the
        # shared <project>/.build.
        self.assertEqual("${workspaceFolder}/.build/stm32g474/canhat", first["JH_ARTIFACT_DIR"])
        self.assertEqual("${workspaceFolder}/.build/stm32g474/canhat-b", settings("canhat-b")["JH_ARTIFACT_DIR"])
        self.assertEqual("${workspaceFolder}/.build/stm32g474/canhat",
                         settings("canhat", {"JH_ARTIFACT_DIR": "${buildDir}"})["JH_ARTIFACT_DIR"])
        self.assertEqual("${workspaceFolder}/published/stm32g474/canhat",
                         settings("canhat", {"JH_ARTIFACT_DIR": "${project}/published"})["JH_ARTIFACT_DIR"])
        board = vscode_task_config.cmake_tools_configure_settings(
            registry, target="stm32g474", board="nucleo-g474re", module="app",
            jh_root_ref="${workspaceFolder}")
        self.assertNotIn("JH_ARTIFACT_DIR", board)

    def test_the_dispatcher_builds_every_assembly(self) -> None:
        examples = WORK / "examples dir"
        if examples.exists():
            shutil.rmtree(examples)
        examples.mkdir(parents=True)
        directory = project("dispatched")
        shutil.move(str(directory), examples / "70_device_tree")
        arguments = mock.Mock(example=None, target="stm32g474", jobs=2, verbose=False)
        result = (True, "label", WORK / "log", 0.1)
        with mock.patch.object(examples_dispatcher, "EXAMPLES_DIR", examples), \
             mock.patch.object(examples_dispatcher, "run_one_example", return_value=result) as runner, \
             redirect_stdout(io.StringIO()) as output:
            self.assertEqual(0, examples_dispatcher.build(arguments))
            self.assertEqual(0, examples_dispatcher.list_examples())
        calls = sorted((call.args[2], call.args[3]) for call in runner.call_args_list)
        self.assertEqual([(["--assembly", "canhat"], [None, "SERIAL"]),
                          (["--assembly", "canhat-b"], [None])], calls)
        self.assertIn("stm32g474/canhat (base, SERIAL); stm32g474/canhat-b (base)", output.getvalue())
        settings = examples_dispatcher.settings_for(
            examples / "70_device_tree", examples_dispatcher.read_manifest(examples / "70_device_tree"))
        self.assertTrue(settings["cmake.buildDirectory"].endswith("/stm32g474-canhat"))
        self.assertEqual("canhat", settings["cmake.configureSettings"]["JH_ASSEMBLY"])
        self.assertTrue(settings["C_Cpp.default.compileCommands"].endswith(
            "/70_device_tree/stm32g474/canhat/compile_commands_patched.json"))
        launch = examples_dispatcher.launch_for("70_device_tree", "stm32g474", "canhat")
        self.assertTrue(all(item["executable"].endswith("/70_device_tree/stm32g474/canhat/firmware.elf")
                            for item in launch["configurations"]))


class FeatureLint(unittest.TestCase):
    def test_records_name_the_assembly(self) -> None:
        directory = project("lint")
        report = WORK / "lint report.json"
        model = generate_hal_features.load_registry(ROOT / "config")
        with redirect_stdout(io.StringIO()):
            self.assertTrue(generate_hal_features.lint_effective_inputs(
                [directory], model, False, report))
        records = json.loads(report.read_text(encoding="utf-8"))["configurations"]
        self.assertEqual([("canhat", None), ("canhat", "SERIAL"), ("canhat-b", None)],
                         [(item["assembly"], item["variant"]) for item in records])
        self.assertTrue(all(item["board"] == "nucleo-g474re" for item in records))


class LibraryWorkspace(unittest.TestCase):
    def setUp(self) -> None:
        self.registry = library.load_registry(ROOT)
        self.dir = project("library workspace")

    def profile(self, assembly: str | None, variant: str | None = None):
        return library.device_tree_profile(self.registry, self.dir, assembly, variant)

    def test_every_configuration_has_its_own_directories(self) -> None:
        profiles = [self.profile("canhat"), self.profile("canhat", "SERIAL"), self.profile("canhat-b")]
        paths = [library.profile_paths(ROOT, item) for item in profiles]
        self.assertEqual(3, len({item.build_dir for item in paths}))
        self.assertEqual(3, len({item.install_dir for item in paths}))
        self.assertEqual(("stm32g474", "canhat"), paths[0].build_dir.parts[-3:-1])
        other = project("library workspace other")
        self.assertNotEqual(paths[0].build_dir, library.profile_paths(
            ROOT, library.device_tree_profile(self.registry, other, "canhat", None)).build_dir)
        command = library.build_commands(ROOT, profiles[1], paths[1])[0]
        self.assertEqual(["--assembly", "canhat", "-p", str(self.dir.resolve()), "--variant", "SERIAL"],
                         command[3:9])
        self.assertNotIn("--board", command)

    def test_the_state_keeps_only_the_selection(self) -> None:
        with self.assertRaisesRegex(library.WorkspaceError, "choose one of the assemblies"):
            self.profile(None)
        with self.assertRaisesRegex(library.WorkspaceError, "unknown variant 'OTHER'"):
            self.profile("canhat", "OTHER")
        root = WORK / "library root"
        (root / ".vscode").mkdir(parents=True, exist_ok=True)
        library.save_active_profile(root, self.profile("canhat-b"))
        state = json.loads((root / library.LOCAL_STATE).read_text(encoding="utf-8"))
        self.assertEqual({"schemaVersion": 1, "configDir": str(self.dir.resolve()), "assembly": "canhat-b"},
                         state)
        self.assertEqual(self.profile("canhat-b"), library.load_active_profile(root, self.registry))

    def test_a_stale_state_never_blocks_an_explicit_choice(self) -> None:
        root = WORK / "library stale root"
        (root / ".vscode").mkdir(parents=True, exist_ok=True)
        for name in ("CMakeLists.txt", "boards"):
            target = root / name
            if not target.exists():
                target.symlink_to(ROOT / name)
        gone = project("library gone")
        library.save_active_profile(root, library.device_tree_profile(self.registry, gone, "canhat", None))
        shutil.rmtree(gone)
        output = io.StringIO()
        with redirect_stdout(output), mock.patch.object(library, "load_registry", return_value=self.registry):
            dump = library.main(["--repo-root", str(root), "config-dump",
                                 "--config-dir", str(self.dir), "--assembly", "canhat-b"])
            select = library.main(["--repo-root", str(root), "select",
                                   "--config-dir", str(self.dir), "--assembly", "canhat"])
        self.assertEqual((0, 0), (dump, select))
        self.assertEqual("canhat", library.load_active_profile(root, self.registry).assembly)

    def test_explicit_inputs_win_over_the_state(self) -> None:
        root = WORK / "library state root"
        (root / ".vscode").mkdir(parents=True, exist_ok=True)
        for name in ("CMakeLists.txt", "boards"):
            target = root / name
            if not target.exists():
                target.symlink_to(ROOT / name)
        library.save_active_profile(root, self.profile("canhat"))
        output = io.StringIO()
        with redirect_stdout(output), mock.patch.object(library, "load_registry", return_value=self.registry):
            status = library.main(["--repo-root", str(root), "config-dump",
                                   "--config-dir", str(self.dir), "--assembly", "canhat-b"])
        self.assertEqual(0, status)
        self.assertEqual("canhat-b", json.loads(output.getvalue())["assembly"])


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
