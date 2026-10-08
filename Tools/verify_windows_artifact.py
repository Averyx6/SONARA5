"""Validate a downloaded exact-run Windows bundle before giving it to a user."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct
import zipfile


def verify(path, commit, version, run, suites):
    with zipfile.ZipFile(path) as archive:
        assert archive.testzip() is None, "ZIP CRC failure"
        names = [entry.filename for entry in archive.infolist() if not entry.is_dir()]
        assert len(names) == len(set(names)), "Duplicate ZIP entries"
        for name in names:
            p = PurePosixPath(name)
            assert not p.is_absolute() and ".." not in p.parts and "\\" not in name, "Unsafe ZIP path"
        required = {
            "SONARA.vst3/Contents/x86_64-win/SONARA.vst3",
            "SONARA.vst3/Contents/Resources/moduleinfo.json",
            "INSTALL.txt", "BUILD_LOG.txt", "FL_STUDIO_WORKFLOW.md", "SHA256SUMS.txt",
        }
        assert required <= set(names), "Incomplete SONARA bundle"
        manifest = archive.read("SHA256SUMS.txt").decode("utf-8-sig")
        hashed = set()
        for line in manifest.splitlines():
            match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
            assert match is not None, "Invalid hash manifest"
            digest, name = match.groups()
            assert name in names and name not in hashed, "Invalid manifest entry"
            assert hashlib.sha256(archive.read(name)).hexdigest() == digest, "SHA-256 mismatch: " + name
            hashed.add(name)
        assert hashed == set(names) - {"SHA256SUMS.txt"}, "Unhashed package contents"
        log = dict(line.split("=", 1) for line in archive.read("BUILD_LOG.txt").decode("utf-8-sig").splitlines() if "=" in line)
        expected = {"commit": commit, "version": version, "workflow_run": str(run),
                    "tests": "passed", "runner": "Windows", "architecture": "x86_64",
                    "moduleinfo_json": "valid", "vst3_discovery_test": "passed"}
        for key, value in expected.items():
            assert log.get(key) == value, "BUILD_LOG mismatch: " + key
        assert int(log["regression_suites"]) >= suites, "Missing regression suites"
        metadata = json.loads(archive.read("SONARA.vst3/Contents/Resources/moduleinfo.json"))
        assert metadata["Name"] == "SONARA" and metadata["Version"] == version, "Wrong module metadata"
        assert any("Instrument" in c.get("Sub Categories", []) for c in metadata["Classes"]), "Not an instrument"
        binary = archive.read("SONARA.vst3/Contents/x86_64-win/SONARA.vst3")
        assert len(binary) > 100000 and binary[:2] == b"MZ", "Invalid Windows binary"
        offset = struct.unpack_from("<I", binary, 0x3c)[0]
        assert binary[offset:offset + 4] == b"PE\0\0", "Invalid PE header"
        assert struct.unpack_from("<H", binary, offset + 4)[0] == 0x8664, "Not Windows x64"
    result = {"zip": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
              "files": len(names), "commit": commit, "version": version,
              "workflow_run": run, "regression_suites": int(log["regression_suites"])}
    print(json.dumps(result, indent=2))
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("zip", type=Path)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--run", type=int, required=True)
    parser.add_argument("--suites", type=int, default=10)
    args = parser.parse_args()
    verify(args.zip, args.commit, args.version, args.run, args.suites)
