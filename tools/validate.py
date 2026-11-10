#!/usr/bin/env python3
"""Check Ampify's JSON files: the community catalog and element manifests.

    python3 tools/validate.py catalog ../ampify-catalog/catalog.json
    python3 tools/validate.py manifest examples/hello-extension/manifest.json
    python3 tools/validate.py versions ../ampify-catalog/versions.json

Standard library only. The schemas in schemas/ use a small subset of JSON Schema, and this
script refuses any keyword outside that subset, so a schema is never checked less strictly
than it reads. Rules a schema cannot express (unique ids, files next to a manifest,
kind-specific fields) are checked by the semantic functions below.

Exit codes: 0 when every file is valid, 1 when a file is invalid, 2 on a usage or schema error.
"""

import argparse
import hashlib
import json
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SCHEMAS = {
    "catalog": os.path.join(ROOT, "schemas", "catalog.schema.json"),
    "manifest": os.path.join(ROOT, "schemas", "manifest.schema.json"),
    "versions": os.path.join(ROOT, "schemas", "versions.schema.json"),
}

# The exports the kit needs (facts/apple.json), and every export of the relevant DLL (knowledge/exports).
APPLE_FACTS = os.path.join(ROOT, "facts", "apple.json")
KNOWN_EXPORTS = os.path.join(ROOT, "knowledge", "exports", "AVFoundationCF.txt")

# Largest preview image a manifest may ship (src/cli/element.hpp, kMaxPreviewBytes).
MAX_PREVIEW_BYTES = 2 * 1024 * 1024

# Major version of the kit API (facts/release.json). Entries for another major version are refused.
with open(os.path.join(ROOT, "facts", "release.json"), encoding="utf-8") as _facts:
    KIT_API_MAJOR = int(json.load(_facts)["kitApi"].split(".")[0])

# Keywords that are accepted and ignored.
ANNOTATIONS = {"$schema", "$id", "$comment", "title", "description", "default", "examples"}
# Keywords that this validator enforces.
RULES = {
    "$ref", "$defs", "type", "const", "enum", "required", "properties", "additionalProperties",
    "items", "minItems", "maxItems", "uniqueItems", "minLength", "maxLength", "pattern",
    "minimum", "maximum",
}
# Keywords whose value maps names to subschemas.
SUBSCHEMA_MAPS = ("properties", "$defs")

TYPE_CHECKS = {
    "object": lambda v: isinstance(v, dict),
    "array": lambda v: isinstance(v, list),
    "string": lambda v: isinstance(v, str),
    "boolean": lambda v: isinstance(v, bool),
    # JSON Schema counts 1.0 as an integer, so an integral float is accepted.
    "integer": lambda v: (isinstance(v, int) and not isinstance(v, bool))
    or (isinstance(v, float) and v.is_integer()),
    "number": lambda v: isinstance(v, (int, float)) and not isinstance(v, bool),
    "null": lambda v: v is None,
}


class SchemaError(Exception):
    """The schema uses something this validator does not check, or a reference is broken."""


def check_supported(schema, where):
    """Refuse any keyword this validator does not enforce."""
    if not isinstance(schema, dict):
        raise SchemaError(f"{where}: a sub-schema must be an object")
    for key, value in schema.items():
        if key in SUBSCHEMA_MAPS:
            for name, sub in value.items():
                check_supported(sub, f"{where}/{key}/{name}")
        elif key == "items":
            check_supported(value, f"{where}/items")
        elif key == "additionalProperties" and isinstance(value, dict):
            check_supported(value, f"{where}/additionalProperties")
        elif key not in ANNOTATIONS and key not in RULES:
            raise SchemaError(f"{where}: unsupported keyword: {key}")


def resolve(root, ref):
    """Follow a local reference such as '#/$defs/kind'."""
    if not ref.startswith("#/"):
        raise SchemaError(f"non-local reference: {ref}")
    node = root
    for part in ref[2:].split("/"):
        part = part.replace("~1", "/").replace("~0", "~")
        if not isinstance(node, dict) or part not in node:
            raise SchemaError(f"reference not found: {ref}")
        node = node[part]
    return node


def at(path):
    return path or "racine"


def join(path, name):
    return f"{path}.{name}" if path else name


def validate(value, schema, root, path, errors):
    """Append a message to `errors` for each rule that `value` breaks."""
    if "$ref" in schema:
        validate(value, resolve(root, schema["$ref"]), root, path, errors)

    if "type" in schema and not TYPE_CHECKS[schema["type"]](value):
        errors.append(f"{at(path)}: expected type {schema['type']}")
        return
    if "const" in schema and value != schema["const"]:
        errors.append(f"{at(path)}: expected value {schema['const']!r}")
    if "enum" in schema and value not in schema["enum"]:
        allowed = ", ".join(str(item) for item in schema["enum"])
        errors.append(f"{at(path)}: value not allowed (expected: {allowed})")

    if isinstance(value, str):
        if "minLength" in schema and len(value) < schema["minLength"]:
            errors.append(f"{at(path)}: fewer than {schema['minLength']} character(s)")
        if "maxLength" in schema and len(value) > schema["maxLength"]:
            errors.append(f"{at(path)}: more than {schema['maxLength']} characters")
        if "pattern" in schema and re.search(schema["pattern"], value) is None:
            errors.append(f"{at(path)}: does not match the pattern {schema['pattern']}")

    if isinstance(value, (int, float)) and not isinstance(value, bool):
        if "minimum" in schema and value < schema["minimum"]:
            errors.append(f"{at(path)}: below {schema['minimum']}")
        if "maximum" in schema and value > schema["maximum"]:
            errors.append(f"{at(path)}: above {schema['maximum']}")

    if isinstance(value, dict):
        properties = schema.get("properties", {})
        for name in schema.get("required", []):
            if name not in value:
                errors.append(f"{at(path)}: required field missing: {name}")
        for name, sub in properties.items():
            if name in value:
                validate(value[name], sub, root, join(path, name), errors)
        extra = [name for name in value if name not in properties]
        additional = schema.get("additionalProperties", True)
        if additional is False:
            for name in extra:
                errors.append(f"{at(join(path, name))}: property not allowed")
        elif isinstance(additional, dict):
            for name in extra:
                validate(value[name], additional, root, join(path, name), errors)

    if isinstance(value, list):
        if "minItems" in schema and len(value) < schema["minItems"]:
            errors.append(f"{at(path)}: fewer than {schema['minItems']} item(s)")
        if "maxItems" in schema and len(value) > schema["maxItems"]:
            errors.append(f"{at(path)}: more than {schema['maxItems']} item(s)")
        if schema.get("uniqueItems"):
            seen = [json.dumps(item, sort_keys=True) for item in value]
            if len(set(seen)) != len(seen):
                errors.append(f"{at(path)}: duplicate items")
        if "items" in schema:
            for index, item in enumerate(value):
                validate(item, schema["items"], root, f"{path}[{index}]", errors)


def check_kit_api(kit_api, where):
    if int(kit_api.split(".")[0]) != KIT_API_MAJOR:
        return [f"{where}: unsupported API version (expected {KIT_API_MAJOR}.x)"]
    return []


def required_exports():
    """requiredPlayerExports of facts/apple.json: what a tested version must have been checked for."""
    with open(APPLE_FACTS, encoding="utf-8") as handle:
        return json.load(handle)["requiredPlayerExports"]


def check_versions(doc, _path):
    """Rules a schema cannot express for versions.json: unique versions, and exports that cover what Ampify needs.

    Every semantic check takes the document and its path; this one needs no path."""
    errors = []
    required = required_exports()
    known = None
    if os.path.isfile(KNOWN_EXPORTS):
        with open(KNOWN_EXPORTS, encoding="utf-8") as handle:
            known = {line.strip() for line in handle if line.strip()}
    seen = set()
    for index, entry in enumerate(doc["versions"]):
        where = f"versions[{index}]"
        if entry["version"] in seen:
            errors.append(f"{where}.version: version already listed: {entry['version']}")
        seen.add(entry["version"])
        for name in required:
            if name not in entry["exports"]:
                errors.append(f"{where}.exports: export required by Ampify is missing: {name}")
        if known is not None:
            for name in entry["exports"]:
                if name not in known:
                    errors.append(f"{where}.exports: {name} is not a known export of AVFoundationCF.dll")
    return errors


def check_catalog(doc, path):
    """Rules a schema cannot express for the catalog."""
    errors = []
    # The pinned versions.json, when it sits next to the catalog as in the repository, must match its SHA-256.
    pin = doc.get("versions")
    if pin and pin["url"].endswith("/versions.json"):
        local = os.path.join(os.path.dirname(os.path.abspath(path)), "versions.json")
        if os.path.isfile(local):
            with open(local, "rb") as handle:
                digest = hashlib.sha256(handle.read()).hexdigest()
            if digest != pin["sha256"]:
                errors.append(f"versions.sha256: does not match the versions.json next to the catalog ({digest})")
    ids = set()
    for index, entry in enumerate(doc["extensions"]):
        where = f"extensions[{index}]"
        if entry["id"] in ids:
            errors.append(f"{where}.id: id already in the catalog: {entry['id']}")
        ids.add(entry["id"])
        errors += check_kit_api(entry["kitApi"], f"{where}.kitApi")
        errors += check_newer(entry, where)
    return errors


def version_key(text):
    return tuple(int(part) for part in text.split("."))


def check_newer(entry, where):
    """A later version of an entry is the same element by the same author, higher than every version before it in the
    list, and says which Ampify it needs: without minAmpify it would reach the Ampify releases the entry is kept for."""
    errors = []
    seen = [entry["version"]]
    for index, later in enumerate(entry.get("newer", [])):
        at = f"{where}.newer[{index}]"
        for key in ("id", "kind", "author"):
            if later[key] != entry[key]:
                errors.append(f"{at}.{key}: must be the entry's {key}, {entry[key]}")
        if "newer" in later:
            errors.append(f"{at}.newer: only the entry itself lists newer versions")
        if "minAmpify" not in later:
            errors.append(f"{at}.minAmpify: required field missing in a newer version")
        if any(version_key(later["version"]) <= version_key(before) for before in seen):
            errors.append(f"{at}.version: {later['version']} must be higher than the versions listed before it")
        seen.append(later["version"])
        errors += check_kit_api(later["kitApi"], f"{at}.kitApi")
    return errors


def fits_settings_file(text):
    """settings.txt holds one value per line and trims its ends (src/core/settings.cpp): a value must survive both."""
    return "\n" not in text and "\r" not in text and text == text.strip(" \t\n\v\f\r")


def check_setting_value(setting, where):
    """The type-dependent rules of one declared setting (src/core/manifest.cpp, ReadSettingValue)."""
    kind, value = setting["type"], setting["default"]
    errors = []
    if ("options" in setting) != (kind == "choice"):
        errors.append(f"{where}: options belong to a choice, and a choice needs them")
    if ({"min", "max"} & setting.keys()) and kind != "number":
        errors.append(f"{where}: min and max belong to a number")
    if kind == "toggle" and not isinstance(value, bool):
        errors.append(f"{where}: default must be true or false")
    if kind == "choice" and not all(fits_settings_file(option) for option in setting.get("options", [])):
        errors.append(f"{where}: options must be on one line, without spaces at either end")
    if kind == "choice" and value not in setting.get("options", []):
        errors.append(f"{where}: default must be one of the options")
    if kind == "text" and (not isinstance(value, str) or len(value) > 200 or not fits_settings_file(value)):
        errors.append(f"{where}: default must be a text of 200 characters at most, on one line, "
                      "without spaces at either end")
    if kind == "number":
        low, high = setting.get("min", float("-inf")), setting.get("max", float("inf"))
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not low <= value <= high:
            errors.append(f"{where}: default must be a number between min and max")
    return errors


def check_settings(settings):
    """Rules of the settings field a schema cannot express: unique keys, the reserved key, defaults that fit."""
    errors, keys = [], set()
    for index, setting in enumerate(settings):
        where = f"settings[{index}]"
        if setting["key"] == "enabled":
            errors.append(f"{where}: key enabled is reserved")
        elif setting["key"] in keys:
            errors.append(f"{where}: key {setting['key']} is already used")
        keys.add(setting["key"])
        errors += check_setting_value(setting, where)
        errors += check_setting_texts(setting, where)
    return errors


def check_setting_texts(setting, where):
    """The optional group and description of a setting: one line, within the length the kernel accepts."""
    errors = []
    for field, limit in (("group", 40), ("description", 200)):
        if field not in setting:
            continue
        text = setting[field]
        if not isinstance(text, str) or not 1 <= len(text) <= limit or "\n" in text or "\r" in text:
            errors.append(f"{where}: {field} must be 1 to {limit} characters, on one line")
    return errors


def check_manifest(doc, path):
    """Rules a schema cannot express for a manifest: its folder, its kind and its files."""
    errors = []
    folder = os.path.dirname(os.path.abspath(path))
    if os.path.basename(path) != "manifest.json":
        errors.append("the file must be named manifest.json")
    if doc["id"] != os.path.basename(folder):
        errors.append(f"id: must be the folder name ({os.path.basename(folder)})")
    errors += check_kit_api(doc["kitApi"], "kitApi")

    kind = doc["kind"]
    if kind in ("extension", "app") and "main" not in doc:
        errors.append(f"main: required for an {kind}")
    if kind == "theme" and not ({"colors", "resources"} & doc.keys()):
        errors.append("colors or resources: at least one of the two is required for a theme")
    if kind == "snippet":
        if "resources" not in doc and not os.path.isfile(os.path.join(folder, "user.css")):
            errors.append("resources or user.css: at least one of the two is required for a snippet")
        if "main" in doc:
            errors.append("main: not allowed for a snippet")

    if "settings" in doc:
        errors += check_settings(doc["settings"]) if kind in ("extension", "theme", "snippet") else [
            "settings: only an extension, a theme or a snippet declares settings"]

    for key in ("main", "colors", "resources", "icon", "preview"):
        if key in doc and not os.path.isfile(os.path.join(folder, doc[key])):
            errors.append(f"{key}: file missing from the folder: {doc[key]}")
    if "preview" in doc and os.path.isfile(os.path.join(folder, doc["preview"])):
        if os.path.getsize(os.path.join(folder, doc["preview"])) > MAX_PREVIEW_BYTES:
            errors.append(f"preview: file larger than 2 MB: {doc['preview']}")
    # The kernel refuses to import through a link, and ampify refuses to install one: a link could point anywhere.
    for current, folders, files in os.walk(folder):
        for name in sorted(folders + files):
            if os.path.islink(os.path.join(current, name)):
                relative = os.path.relpath(os.path.join(current, name), folder).replace(os.sep, "/")
                errors.append(f"symbolic link not allowed in the element: {relative}")
    return errors


SEMANTIC_CHECKS = {
    "catalog": check_catalog,
    "manifest": check_manifest,
    "versions": check_versions,
}


def load_schema(kind):
    """The schema of a kind of file, checked to stay inside the supported subset."""
    with open(SCHEMAS[kind], encoding="utf-8") as handle:
        schema = json.load(handle)
    check_supported(schema, "schema")
    return schema


def check_file(kind, path, schema=None):
    """The problems of one file as a list of messages, empty when it is valid.

    Raises OSError, ValueError, SchemaError or re.error when the file cannot be checked at all."""
    schema = schema or load_schema(kind)
    with open(path, encoding="utf-8") as handle:
        document = json.load(handle)
    errors = []
    validate(document, schema, schema, "", errors)
    return errors or SEMANTIC_CHECKS[kind](document, path)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Check the catalog, the list of versions or element manifests.")
    parser.add_argument("kind", choices=sorted(SCHEMAS), help="what the file contains")
    parser.add_argument("files", nargs="+", metavar="file", help="JSON files to check")
    args = parser.parse_args(argv)

    try:
        schema = load_schema(args.kind)
    except (OSError, ValueError, SchemaError) as error:
        print(f"unusable {args.kind} schema: {error}", file=sys.stderr)
        return 2

    status = 0
    for path in args.files:
        try:
            errors = check_file(args.kind, path, schema)
        except (OSError, ValueError, SchemaError, re.error) as error:
            print(f"{path}: cannot check: {error}", file=sys.stderr)
            return 2

        if errors:
            status = 1
            for message in errors:
                print(f"{path}: {message}")
        else:
            print(f"{path}: valid")
    return status


if __name__ == "__main__":
    sys.exit(main())
