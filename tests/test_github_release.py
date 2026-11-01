#!/usr/bin/env python3
# Physalis - The update check against GitHub releases, built and run on the host.
# Copyright (C) 2026 Oliv3rmoon
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds opengl-ui/tests/github_release_check.cc with the update kit's files it tests
(update_check.c without its network side, github_release.c, self_update.c,
self_update_sha256.c) under the address and undefined-behaviour sanitizers, warnings as
errors, and runs it. CC and CXX name other compilers (default: clang, clang++)."""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
KIT = ROOT / "opengl-ui/ps5/src/update_kit"
CHECK = ROOT / "opengl-ui/tests/github_release_check.cc"
SCHEDULE_CHECK = ROOT / "opengl-ui/tests/update_schedule_check.cc"
FLAGS = ["-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
         "-fno-omit-frame-pointer", "-Wall", "-Wextra", "-Werror",
         "-DUPDATE_CHECK_NO_NETWORK=1", "-UNDEBUG"]


class GitHubReleaseTest(unittest.TestCase):
    def test_update_check_asks_only_the_repositorys_releases(self):
        cc = os.environ.get("CC", "clang")
        cxx = os.environ.get("CXX", "clang++")
        if not shutil.which(cc) or not shutil.which(cxx):
            self.skipTest(f"needs {cc} and {cxx}")
        with tempfile.TemporaryDirectory() as directory:
            objects = []
            for name in ("update_check", "github_release", "self_update", "self_update_sha256"):
                objects.append(str(Path(directory) / f"{name}.o"))
                subprocess.run([cc, "-std=c11", *FLAGS, "-c", str(KIT / f"{name}.c"),
                                "-o", objects[-1]], check=True)
            executable = Path(directory) / "github-release-check"
            subprocess.run([cxx, "-std=c++20", "-pthread", *FLAGS,
                            "-I" + str(ROOT / "opengl-ui/ps5/src"), str(CHECK), *objects,
                            "-o", str(executable)], check=True)
            result = subprocess.run([str(executable)], capture_output=True, text=True,
                                    timeout=120, check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr[-3000:])
        self.assertIn("PASS", result.stdout)

    def test_update_check_asks_again_every_few_hours_and_offers_a_version_once(self):
        cc = os.environ.get("CC", "clang")
        cxx = os.environ.get("CXX", "clang++")
        if not shutil.which(cc) or not shutil.which(cxx):
            self.skipTest(f"needs {cc} and {cxx}")
        with tempfile.TemporaryDirectory() as directory:
            schedule = str(Path(directory) / "update_schedule.o")
            subprocess.run([cc, "-std=c11", *FLAGS, "-c", str(KIT / "update_schedule.c"),
                            "-o", schedule], check=True)
            executable = Path(directory) / "update-schedule-check"
            subprocess.run([cxx, "-std=c++20", *FLAGS, "-I" + str(ROOT / "opengl-ui/ps5/src"),
                            str(SCHEDULE_CHECK), schedule, "-o", str(executable)], check=True)
            result = subprocess.run([str(executable)], capture_output=True, text=True,
                                    timeout=120, check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr[-3000:])
        self.assertIn("update schedule PASS", result.stdout)

    def test_update_offers_are_begun_and_taken_only_by_the_menu(self):
        update = (ROOT / "opengl-ui/ps5/src/tv_update.cpp").read_text(encoding="utf-8")
        start = update.split("void start_update_check()", 1)[1].split("} // namespace tv", 1)[0]
        self.assertIn("update_schedule_due(&g_schedule, now)", start)
        self.assertLess(start.index("update_schedule_due"), start.index("pthread_create"))
        check = update.split("void *check(void *)", 1)[1].split("ptv::platform::UpdatePhase", 1)[0]
        self.assertIn("update_schedule_end(&g_schedule)", check)
        self.assertLess(check.index("update_schedule_offer(&g_schedule, result.available)"),
                        check.index("g_found = true"))
        # Update now installs the release the dialog shows, even if a later check found another.
        begin = update.split("bool update_begin()", 1)[1].split("UpdateProgress update_poll()", 1)[0]
        self.assertIn("self_update_start(&g_job, self_update_console(), &g_shown)", begin)
        take = update.split("bool update_take(UpdateOffer *out)", 1)[1].split("bool update_begin()", 1)[0]
        self.assertIn("g_shown = g_offer;", take)
        # Begun from the menu's frame loop (and at launch), never from playback.
        main = (ROOT / "opengl-ui/ps5/src/main.cpp").read_text(encoding="utf-8")
        menu = main.split("bool run_menu(", 1)[1].split("\n}\n", 1)[0]
        frame = menu.split("while (!chosen)", 1)[1]
        self.assertIn("tv::start_update_check();", frame)
        self.assertEqual(main.count("tv::start_update_check();"), 2)
        # The answer is taken only by the menu (App::update), never by the playback screens.
        takers = []
        for folder in ("opengl-ui/src/tv", "opengl-ui/ps5/src"):
            for source in sorted((ROOT / folder).rglob("*.cpp")):
                text = source.read_text(encoding="utf-8")
                if "update_take(&" in text:
                    takers.append(source.relative_to(ROOT).as_posix())
        self.assertEqual(takers, ["opengl-ui/src/tv/app.cpp"])

    def test_the_build_names_physalis_repository(self):
        options = (ROOT / "opengl-ui/ps5/src/tv_build_options.h").read_text(encoding="utf-8")
        self.assertIn('#define TV_UPDATE_REPOSITORY "Oliv3rmoon/Physalis"', options)
        patch = (ROOT / "opengl-ui/ps5/patch_tree.py").read_text(encoding="utf-8")
        self.assertIn('"TV_UPDATE_REPOSITORY", "Oliv3rmoon/Physalis"', patch)
        update = (ROOT / "opengl-ui/ps5/src/tv_update.cpp").read_text(encoding="utf-8")
        # The catalog lists ProsperoTV's own releases: the app never asks it.
        self.assertNotIn("self_update_check(", update)
        self.assertNotIn("self_update_check_self(", update)
        self.assertIn("self_update_check_self_from(kRepository", update)


if __name__ == "__main__":
    unittest.main()
