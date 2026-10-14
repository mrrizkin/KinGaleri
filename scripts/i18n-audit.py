#!/usr/bin/env python3
"""Check that every qsTr() string in the QML sources has a catalog entry.

`lupdate` in this Qt (6.12 via qt6-tools) cannot read QML, so
resources/i18n/kin-galeri_id.ts is maintained by hand. That makes it easy to
add a qsTr() call and forget the catalog: whole contexts (CollectionsView,
MediaCell) had drifted out of the file, and 106 strings in existing contexts
had no entry. The C++ side is guarded by tests/tst_i18n.cpp; this is the QML
half, and it is the reason the missing translations were invisible (the UI
simply fell back to English while everything else switched).

Qt uses the QML component's name as the tr() context, which for a file in
src/ui/qml/ is the file's base name (Sidebar.qml -> "Sidebar"), so a context is
compared per file rather than globally.

  usage: i18n-audit.py [--ts PATH] [--qml DIR]
  exit:  0 when complete, 1 when anything is missing or empty
"""
import argparse
import os
import re
import sys
import xml.etree.ElementTree as ET

QS_TR = re.compile(r'qsTr\(\s*"((?:[^"\\]|\\.)*)"')


def unescape(qml_string):
    """Turn QML escapes into the characters the .ts actually stores."""
    out = re.sub(r"\\u([0-9a-fA-F]{4})", lambda m: chr(int(m.group(1), 16)), qml_string)
    return out.replace("\\n", "\n").replace('\\"', '"').replace("\\\\", "\\")


def catalog(ts_path):
    tree = ET.parse(ts_path)
    out = {}
    for context in tree.getroot().findall("context"):
        name = context.findtext("name")
        out[name] = {
            message.findtext("source"): (message.findtext("translation") or "")
            for message in context.findall("message")
        }
    return out


def qml_calls(qml_dir):
    calls = {}
    for entry in sorted(os.listdir(qml_dir)):
        if not entry.endswith(".qml"):
            continue
        context = entry[:-4]
        text = open(os.path.join(qml_dir, entry), encoding="utf-8").read()
        # Drop comment lines so a commented-out qsTr() is not reported.
        text = "\n".join(line for line in text.split("\n") if not line.strip().startswith("//"))
        calls.setdefault(context, {})
        for match in QS_TR.finditer(text):
            source = unescape(match.group(1))
            calls[context][source] = calls[context].get(source, 0) + 1
    return calls


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ts", default="resources/i18n/kin-galeri_id.ts")
    parser.add_argument("--qml", default="src/ui/qml")
    args = parser.parse_args()

    translated = catalog(args.ts)
    calls = qml_calls(args.qml)

    missing = []
    empty = []
    for context, sources in sorted(calls.items()):
        for source in sources:
            if context not in translated:
                missing.append((context, source, "no such context in the .ts"))
            elif source not in translated[context]:
                missing.append((context, source, "no <message> for this source"))
            elif not translated[context][source].strip():
                empty.append((context, source))

    pairs = sum(len(v) for v in calls.values())
    print(f"qsTr(): {pairs} distinct (context, source) pairs over {len(calls)} files")
    for label, items in (("missing", missing), ("empty", empty)):
        for context, source, *why in items:
            print(f"  {label}: {context}: {source!r}" + (f"  ({why[0]})" if why else ""))
    if missing or empty:
        print(f"\n{len(missing)} missing, {len(empty)} empty - "
              f"add them to {args.ts} (Qt context = QML file base name)")
        return 1
    print(f"all {pairs} QML strings are translated in {args.ts}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
