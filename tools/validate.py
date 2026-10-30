#!/usr/bin/env python3
"""Check the kit's JSON files: the community catalog and extension manifests.

    python3 tools/validate.py catalog catalog/catalog.json
    python3 tools/validate.py manifest examples/hello-extension/manifest.json

Standard library only. The schemas in schemas/ use a small subset of JSON Schema, and this
script refuses any keyword outside that subset, so a schema is never checked less strictly
than it reads. Rules a schema cannot express (unique ids, files next to a manifest,
kind-specific fields) are checked by the semantic functions below.

Exit codes: 0 when every file is valid, 1 when a file is invalid, 2 on a usage or schema error.
"""

import argparse
import json
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SCHEMAS = {
    "catalog": os.path.join(ROOT, "schemas", "catalog.schema.json"),
    "manifest": os.path.join(ROOT, "schemas", "manifest.schema.json"),
}

# Major version of the kit API this script knows. Entries for another major version are refused.
KIT_API_MAJOR = 1

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
# Keywords whose value is a subschema (or a boolean, for additionalProperties).
SUBSCHEMA_KEYWORDS = ("items", "additionalProperties")

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
        raise SchemaError(f"{where} : un sous-schéma doit être un objet")
    for key, value in schema.items():
        if key in SUBSCHEMA_MAPS:
            for name, sub in value.items():
                check_supported(sub, f"{where}/{key}/{name}")
        elif key == "items":
            check_supported(value, f"{where}/items")
        elif key == "additionalProperties" and isinstance(value, dict):
            check_supported(value, f"{where}/additionalProperties")
        elif key not in ANNOTATIONS and key not in RULES:
            raise SchemaError(f"{where} : mot-clé non pris en charge : {key}")


def resolve(root, ref):
    """Follow a local reference such as '#/$defs/kind'."""
    if not ref.startswith("#/"):
        raise SchemaError(f"référence non locale : {ref}")
    node = root
    for part in ref[2:].split("/"):
        part = part.replace("~1", "/").replace("~0", "~")
        if not isinstance(node, dict) or part not in node:
            raise SchemaError(f"référence introuvable : {ref}")
        node = node[part]
    return node


def at(path):
    return path or "racine"


def join(path, name):
    return f"{path}.{name}" if path else name


def validate(value, schema, root, path, errors):
    """Append a French message to `errors` for each rule that `value` breaks."""
    if "$ref" in schema:
        validate(value, resolve(root, schema["$ref"]), root, path, errors)

    if "type" in schema and not TYPE_CHECKS[schema["type"]](value):
        errors.append(f"{at(path)} : type attendu {schema['type']}")
        return
    if "const" in schema and value != schema["const"]:
        errors.append(f"{at(path)} : valeur attendue {schema['const']!r}")
    if "enum" in schema and value not in schema["enum"]:
        allowed = ", ".join(str(item) for item in schema["enum"])
        errors.append(f"{at(path)} : valeur non autorisée (attendu : {allowed})")

    if isinstance(value, str):
        if "minLength" in schema and len(value) < schema["minLength"]:
            errors.append(f"{at(path)} : moins de {schema['minLength']} caractère(s)")
        if "maxLength" in schema and len(value) > schema["maxLength"]:
            errors.append(f"{at(path)} : plus de {schema['maxLength']} caractères")
        if "pattern" in schema and re.search(schema["pattern"], value) is None:
            errors.append(f"{at(path)} : ne correspond pas au motif {schema['pattern']}")

    if isinstance(value, (int, float)) and not isinstance(value, bool):
        if "minimum" in schema and value < schema["minimum"]:
            errors.append(f"{at(path)} : inférieur à {schema['minimum']}")
        if "maximum" in schema and value > schema["maximum"]:
            errors.append(f"{at(path)} : supérieur à {schema['maximum']}")

    if isinstance(value, dict):
        properties = schema.get("properties", {})
        for name in schema.get("required", []):
            if name not in value:
                errors.append(f"{at(path)} : champ requis absent : {name}")
        for name, sub in properties.items():
            if name in value:
                validate(value[name], sub, root, join(path, name), errors)
        extra = [name for name in value if name not in properties]
        additional = schema.get("additionalProperties", True)
        if additional is False:
            for name in extra:
                errors.append(f"{at(join(path, name))} : propriété non autorisée")
        elif isinstance(additional, dict):
            for name in extra:
                validate(value[name], additional, root, join(path, name), errors)

    if isinstance(value, list):
        if "minItems" in schema and len(value) < schema["minItems"]:
            errors.append(f"{at(path)} : moins de {schema['minItems']} élément(s)")
        if "maxItems" in schema and len(value) > schema["maxItems"]:
            errors.append(f"{at(path)} : plus de {schema['maxItems']} élément(s)")
        if schema.get("uniqueItems"):
            seen = [json.dumps(item, sort_keys=True) for item in value]
            if len(set(seen)) != len(seen):
                errors.append(f"{at(path)} : éléments en double")
        if "items" in schema:
            for index, item in enumerate(value):
                validate(item, schema["items"], root, f"{path}[{index}]", errors)


def check_kit_api(kit_api, where):
    if int(kit_api.split(".")[0]) != KIT_API_MAJOR:
        return [f"{where} : version d'API non prise en charge (attendu {KIT_API_MAJOR}.x)"]
    return []


def check_catalog(doc, path):
    """Rules a schema cannot express for the catalogue."""
    errors = []
    ids = set()
    for index, entry in enumerate(doc["extensions"]):
        where = f"extensions[{index}]"
        if entry["id"] in ids:
            errors.append(f"{where}.id : identifiant déjà présent dans le catalogue : {entry['id']}")
        ids.add(entry["id"])
        errors += check_kit_api(entry["kitApi"], f"{where}.kitApi")
    return errors


def check_manifest(doc, path):
    """Rules a schema cannot express for a manifest: its folder, its kind and its files."""
    errors = []
    folder = os.path.dirname(os.path.abspath(path))
    if os.path.basename(path) != "manifest.json":
        errors.append("le fichier doit s'appeler manifest.json")
    if doc["id"] != os.path.basename(folder):
        errors.append(f"id : doit être le nom du dossier ({os.path.basename(folder)})")
    errors += check_kit_api(doc["kitApi"], "kitApi")

    kind = doc["kind"]
    if kind in ("extension", "app") and "main" not in doc:
        errors.append(f"main : requis pour une {kind}")
    if kind == "theme" and not ({"colors", "resources"} & doc.keys()):
        errors.append("colors ou resources : au moins un des deux est requis pour un thème")
    if kind == "snippet":
        if "resources" not in doc:
            errors.append("resources : requis pour un snippet")
        if "main" in doc:
            errors.append("main : interdit pour un snippet")

    for key in ("main", "colors", "resources", "icon"):
        if key in doc and not os.path.isfile(os.path.join(folder, doc[key])):
            errors.append(f"{key} : fichier absent du dossier : {doc[key]}")
    return errors


SEMANTIC_CHECKS = {"catalog": check_catalog, "manifest": check_manifest}


def main(argv=None):
    parser = argparse.ArgumentParser(description="Valide le catalogue ou des manifestes du kit.")
    parser.add_argument("kind", choices=sorted(SCHEMAS), help="ce que contient le fichier")
    parser.add_argument("files", nargs="+", metavar="file", help="fichiers JSON à valider")
    args = parser.parse_args(argv)

    try:
        with open(SCHEMAS[args.kind], encoding="utf-8") as handle:
            schema = json.load(handle)
        check_supported(schema, "schéma")
    except (OSError, ValueError, SchemaError) as error:
        print(f"schéma {args.kind} inutilisable : {error}", file=sys.stderr)
        return 2

    status = 0
    for path in args.files:
        try:
            with open(path, encoding="utf-8") as handle:
                document = json.load(handle)
            errors = []
            validate(document, schema, schema, "", errors)
            if not errors:
                errors = SEMANTIC_CHECKS[args.kind](document, path)
        except (OSError, ValueError, SchemaError, re.error) as error:
            print(f"{path} : contrôle impossible : {error}", file=sys.stderr)
            return 2

        if errors:
            status = 1
            for message in errors:
                print(f"{path} : {message}")
        else:
            print(f"{path} : valide")
    return status


if __name__ == "__main__":
    sys.exit(main())
