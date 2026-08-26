#!/usr/bin/env python3
"""Scans one or more script directories for DUALITY_PROPERTY()-marked fields and generates the
out-of-class Fields() definitions DUALITY_PROPERTIES_AUTO() only declares.

Deliberately a plain regex/line scanner, NOT a real C++ parser -- every field this project's
reflection system supports (see DualityEngine/Reflection/Field.h's FieldValue variant) is a
single-token type with no template arguments or embedded spaces (float, bool, Duality::EntityRef,
glm::vec3, ...), so "DUALITY_PROPERTY() <Type> <Name> [= <Default>];" is a fully reliable shape
to match without needing to actually understand C++ grammar. This mirrors how Unreal Header Tool
and Polyphase-Engine's C# Roslyn source-rewriter solve the same "real per-field attribute"
problem -- a real code-generation pass over the source text, run before the real compiler.

Scans multiple directories -- GameScripts/Include (the engine's own shared/demo scripts) AND,
when set, the active Project's own Assets/Scripts directory (see
GameScripts/CMakeLists.txt's DUALITY_PROJECT_SCRIPTS_DIR) -- so a project's own scripts get the
same generated Fields() treatment with zero extra steps. #include lines always use each header's
full absolute path rather than a bare filename, so this works regardless of which directories are
on GameScripts' own include path (a project's Scripts directory deliberately isn't -- project
scripts live flat, .h next to .cpp, resolved via the compiler's own same-directory quoted-include
search instead).

Run as: python generate_fields.py <output .cpp path> <scan_dir_1> [scan_dir_2 ...]
Each scan_dir that's empty or doesn't exist is silently skipped (e.g. no active Project yet).
Overwrites the output file unconditionally on every run (cheap; no incremental diffing) --
invoked via execute_process() at CMake CONFIGURE time (not a build-time add_custom_command),
deliberately: a build-time OUTPUT/DEPENDS staleness check only looks at file mtimes, not at
whether DUALITY_PROJECT_SCRIPTS_DIR's *value* changed between configures, which let a stale
generated file silently survive a project switch in an earlier version of this setup (confirmed
via a real linker error). Every real entry point in this project already reconfigures before
building, so "regenerate on every configure" is the right cadence, not overkill.
"""
import os
import re
import sys

CLASS_RE = re.compile(r"\bclass\s+(\w+)\s*:\s*public\s+(?:Duality::)?Behaviour\b")
FIELD_RE = re.compile(r"\bDUALITY_PROPERTY\(\)\s+([\w:<>]+)\s+(\w+)\s*(?:=.*)?;")
MSYS_DRIVE_PATH_RE = re.compile(r"^/([A-Za-z])/(.*)$")


def format_include_path(path):
    """Formats a path for a generated C++ #include.

    MSYS-hosted CMake can pass its own source directory as ``/d/...`` even
    when this script is run by native Windows Python.  That spelling is not a
    portable absolute Windows include path, so turn just that MSYS drive form
    into the forward-slash Windows spelling expected by the generated source.
    Leave normal POSIX paths untouched for non-Windows builds.
    """
    path = path.replace("\\", "/")
    match = MSYS_DRIVE_PATH_RE.match(path)
    if match:
        return f"{match.group(1).upper()}:/{match.group(2)}"
    return path


def scan_header(path):
    """Returns a list of (class_name, [field_name, ...]) for every Behaviour subclass in this
    file that has at least one DUALITY_PROPERTY() field."""
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()

    results = []
    current_class = None
    current_fields = []
    depth = 0
    tracking_depth = None  # brace depth at which the tracked class's body started

    for line in text.splitlines():
        class_match = CLASS_RE.search(line)
        if class_match and current_class is None:
            current_class = class_match.group(1)
            current_fields = []
            tracking_depth = None

        if current_class is not None:
            field_match = FIELD_RE.search(line)
            if field_match:
                current_fields.append(field_match.group(2))

        depth += line.count("{") - line.count("}")

        if current_class is not None and tracking_depth is None and "{" in line:
            tracking_depth = depth
        if current_class is not None and tracking_depth is not None and depth < tracking_depth:
            if current_fields:
                results.append((current_class, current_fields))
            current_class = None
            current_fields = []
            tracking_depth = None

    return results


def main():
    if len(sys.argv) < 3:
        print("usage: generate_fields.py <output .cpp path> <scan_dir_1> [scan_dir_2 ...]", file=sys.stderr)
        return 1

    output_path = sys.argv[1]
    # abspath() up front, even for an already-absolute input -- a scan_dir passed in relative
    # (e.g. a Project whose own directory was given relative, like the auto-created demo
    # SampleProject) resolves fine for os.path.isdir/os.listdir below (relative to whatever this
    # script's own CWD happens to be, which -- since it's invoked via CMake's execute_process
    # inheriting the Editor process's own CWD -- is the repo root when this runs for real), but
    # the SAME relative string, if it ended up literally inside a generated #include line, would
    # NOT resolve later when the compiler reads that #include with ITS OWN (different, build-
    # directory) CWD. Confirmed via a real build failure: "SampleProject/Assets/Scripts/
    # ProjectDemoBehaviour.h: No such file or directory". Resolving to absolute here, once, keeps
    # every downstream use (scanning AND the #include line) consistent and CWD-independent.
    scan_dirs = [os.path.abspath(d) for d in sys.argv[2:] if d and os.path.isdir(d)]

    all_classes = []  # (full_header_path, class_name, [field_name, ...])
    for scan_dir in scan_dirs:
        for header in sorted(f for f in os.listdir(scan_dir) if f.endswith(".h")):
            full_path = os.path.join(scan_dir, header)
            for class_name, fields in scan_header(full_path):
                all_classes.append((full_path, class_name, fields))

    lines = [
        "// AUTO-GENERATED by GameScripts/CodeGen/generate_fields.py -- do not edit by hand.",
        "// Regenerated on every build from every DUALITY_PROPERTY() marker in every scanned directory.",
        "",
    ]
    for full_path, _, _ in all_classes:
        lines.append(f'#include "{format_include_path(full_path)}"')
    if all_classes:
        lines.append("")

    for _, class_name, fields in all_classes:
        lines.append(f"std::vector<::Duality::FieldHandle> {class_name}::Fields() {{")
        lines.append("    return {")
        for field in fields:
            lines.append(f'        ::Duality::MakeField("{field}", &{class_name}::{field}),')
        lines.append("    };")
        lines.append("}")
        lines.append("")

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

    print(f"generate_fields.py: generated Fields() for {len(all_classes)} class(es) -> {output_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
