"""Build a source ZIP from explicit public directories, never the workspace root.

Run after: python -m pip --no-cache-dir wheel . --no-deps --no-build-isolation -w dist
"""
from pathlib import Path
import zipfile

root = Path(__file__).resolve().parents[1]
version = "0.1.0"
output = root / "dist" / f"dingtalk-a1-sdk-{version}.zip"
output.parent.mkdir(exist_ok=True)
files = [root / name for name in ("README.md", "pyproject.toml", "LICENSE", "NOTICE.md", ".gitignore")]
for directory in ("src/dingtalk_a1", "docs", "examples", "skills/dingtalk-a1", "tests", "tools"):
    files.extend(path for path in (root / directory).rglob("*")
                 if path.is_file() and "__pycache__" not in path.parts and path.suffix != ".pyc")
files.extend((root / "dist").glob("*.whl"))
with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(files):
        archive.write(path, f"dingtalk-a1-sdk-{version}/" + path.relative_to(root).as_posix())
print(f"{output}\n{len(files)} public files; {output.stat().st_size} bytes")
