#!/usr/bin/env python3
"""Scans one or more script directories for DUALITY_PROPERTY()-marked fields and generates the
out-of-class Fields() definitions DUALITY_PROPERTIES_AUTO()/DUALITY_SERIALIZABLE() only declares.

Deliberately a plain regex/line scanner, NOT a real C++ parser -- every field this project's
reflection system supports (see DualityEngine/Reflection/Field.h's FieldValue variant) is a
single-token type with no template arguments or embedded spaces (float, bool, Duality::EntityRef,
glm::vec3, DamageInfo, ...), so "DUALITY_PROPERTY() <Type> <Name> [= <Default>];" is a fully
reliable shape to match without needing to actually understand C++ grammar. This mirrors how
Unreal Header Tool and Polyphase-Engine's C# Roslyn source-rewriter solve the same "real per-field
attribute" problem -- a real code-generation pass over the source text, run before the real
compiler.

Scans multiple directories -- GameScripts/Include (the engine's own shared/demo scripts) AND,
when set, the active Project's own Assets/Scripts directory (see
GameScripts/CMakeLists.txt's DUALITY_PROJECT_SCRIPTS_DIR) -- so a project's own scripts get the
same generated Fields() treatment with zero extra steps. #include lines always use each header's
full absolute path rather than a bare filename, so this works regardless of which directories are
on GameScripts' own include path (a project's Scripts directory deliberately isn't -- project
scripts live flat, .h next to .cpp, resolved via the compiler's own same-directory quoted-include
search instead).

Every DUALITY_PROPERTIES_AUTO()/DUALITY_SERIALIZABLE()-marked type found across ALL scanned
files/directories -- a Behaviour subclass or a plain nested struct alike, identical codegen path
either way, see PropertyMacros.h's own comment on why DUALITY_SERIALIZABLE() is just a
semantically-distinct alias -- is collected into one project-wide set BEFORE any field gets
emitted. A DUALITY_PROPERTY()-marked field whose own type token (matched on its LAST "::"
segment, since GameScripts/Include classes are never themselves namespace-qualified in this
codebase's real convention) is in that set gets ::Duality::MakeNestedField instead of the
ordinary ::Duality::MakeField -- Unity's [System.Serializable]-class-as-a-field, for this engine.

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

CLASS_RE = re.compile(r"\b(?:class|struct)\s+(\w+)\b(?:\s*:\s*public\s+(?:Duality::)?Behaviour\b)?")
FIELD_RE = re.compile(r"\bDUALITY_PROPERTY\(\)\s+([\w:<>]+)\s+(\w+)\s*(?:=.*)?;")
ENUM_CLASS_RE = re.compile(r"\benum\s+class\s+(\w+)\s*(?::\s*[\w\s]+)?\s*\{([^}]+)\}")
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
    """Returns a list of (class_name, [(field_name, field_type), ...]) for every class/struct in
    this file that has at least one DUALITY_PROPERTY() field -- a Behaviour subclass (matched by
    its own ": public Behaviour" base clause, kept for a reader's sake even though it's no longer
    load-bearing for the regex match itself) or a plain DUALITY_SERIALIZABLE() struct alike."""
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()

    results = []
    current_class = None
    current_fields = []
    depth = 0
    tracking_depth = None  # brace depth at which the tracked class's body started

    for raw_line in text.splitlines():
        # Strip a "//" line comment before matching anything against this line -- a real,
        # confirmed bug found while adding this feature: broadening CLASS_RE to match a bare
        # "class X"/"struct X" (not just "class X : public Behaviour") makes it FAR more likely
        # to false-positive on ordinary prose ("...marks the struct itself as nestable..."
        # matched as a class named "itself", corrupting tracking state and silently swallowing
        # the REAL class definition later in the same file -- the exact "swallowed by stray
        # state" failure mode the forward-declaration guard above exists for, just triggered by
        # a comment instead). This codebase's own style is comment-heavy, so this was always a
        # latent risk, just far less likely to trigger with the original narrow regex. A plain
        # split on the first "//" (not comment-aware of string literals containing "//", which
        # essentially never occurs in this codebase's real header style) matches this whole
        # script's own "plain regex scanner, not a real parser" philosophy -- computed once and
        # used for EVERY check below (class match, field match, brace counting) so a stray brace
        # inside a comment can't desync depth tracking either.
        line = raw_line.split("//", 1)[0]

        class_match = CLASS_RE.search(line)
        if class_match and current_class is None:
            # A forward declaration ("struct DamageInfo;") matches this same class-name pattern
            # but never opens a body -- skip it rather than start tracking, so it doesn't swallow
            # tracking state and hide the NEXT real class definition in this file. Only a live
            # risk now that a bare "class X"/"struct X" (not just "class X : public Behaviour")
            # is matched -- no GameScripts header forward-declares anything today (confirmed),
            # but this guard makes that safe going forward. A real definition's own declaration
            # line never ends in ";" (only the closing "};" at the end of its body does, on a
            # different line by the time brace-tracking below closes it out).
            if "{" not in line and line.rstrip().endswith(";"):
                pass
            else:
                current_class = class_match.group(1)
                current_fields = []
                tracking_depth = None

        if current_class is not None:
            field_match = FIELD_RE.search(line)
            if field_match:
                current_fields.append((field_match.group(2), field_match.group(1)))

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


def parse_enum_values(body):
    values = []
    for part in body.split(","):
        part = part.strip()
        if not part:
            continue
        name = part.split("=", 1)[0].strip()
        if name and (name[0].isalpha() or name[0] == '_'):
            values.append(name)
    return values


def scan_enums(scan_dirs):
    enums = {}
    for scan_dir in scan_dirs:
        for header in sorted(f for f in os.listdir(scan_dir) if f.endswith(".h")):
            full_path = os.path.join(scan_dir, header)
            with open(full_path, "r", encoding="utf-8") as f:
                text = f.read()
            for match in ENUM_CLASS_RE.finditer(text):
                enum_name = match.group(1)
                values = parse_enum_values(match.group(2))
                if values:
                    enums[enum_name] = values
    return enums


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

    enum_type_names = scan_enums(scan_dirs)

    all_classes = []  # (full_header_path, class_name, [(field_name, field_type), ...])
    for scan_dir in scan_dirs:
        for header in sorted(f for f in os.listdir(scan_dir) if f.endswith(".h")):
            full_path = os.path.join(scan_dir, header)
            for class_name, fields in scan_header(full_path):
                all_classes.append((full_path, class_name, fields))

    # Every Fields()-generated type name, project-wide, collected BEFORE any field is emitted --
    # a nested struct can be defined in one header and used as a field in another. See this
    # module's own docstring for why a field's type is matched against this set on its LAST "::"
    # segment.
    nested_type_names = {class_name for _, class_name, _ in all_classes}

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
        for field_name, field_type in fields:
            type_leaf = field_type.rsplit("::", 1)[-1]
            if type_leaf in nested_type_names:
                maker = "MakeNestedField"
                lines.append(f'        ::Duality::{maker}("{field_name}", &{class_name}::{field_name}),')
            elif type_leaf in enum_type_names:
                options = ", ".join(f'"{value}"' for value in enum_type_names[type_leaf])
                lines.append(f'        ::Duality::MakeEnumField("{field_name}", &{class_name}::{field_name}, {{ {options} }}),')
            else:
                lines.append(f'        ::Duality::MakeField("{field_name}", &{class_name}::{field_name}),')
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
