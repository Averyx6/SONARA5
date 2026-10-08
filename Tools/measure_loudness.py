"""Measure benchmark WAVs with FFmpeg's EBU R128 meter; no audio is changed."""
import argparse
import csv
from pathlib import Path
import re
import subprocess

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    rows = []
    for path in sorted(args.directory.glob("*.wav")):
        result = subprocess.run(["ffmpeg", "-hide_banner", "-nostats", "-i", str(path),
                                 "-filter_complex", "ebur128=peak=true", "-f", "null", "-"],
                                capture_output=True, text=True, check=True)
        summary = result.stderr.rsplit("Summary:", 1)[-1]
        integrated = re.search(r"I:\s*([-0-9.]+) LUFS", summary)
        peak = re.search(r"Peak:\s*([-0-9.]+) dBFS", summary)
        if not integrated or not peak:
            raise RuntimeError("Missing EBU R128 summary for " + str(path))
        rows.append({"file": path.name, "integrated_lufs": float(integrated.group(1)),
                     "true_peak_dbfs": float(peak.group(1))})
    if not rows:
        raise RuntimeError("No benchmark WAV files")
    with (args.directory / "loudness.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    print(f"Measured {len(rows)} WAVs; saved {args.directory / 'loudness.csv'}")
