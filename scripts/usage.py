#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Measure Elara under heavy use, as a person would run it.

The micro-benchmarks (`elara.py bench`) time one function on a song built in
memory. This runs the real programs on large songs written to disk and reports
what each run cost the machine: wall time, processor time (all threads), peak
memory (working set and committed private bytes), bytes read and written, and
the size of what it wrote. It also sizes the programs and, given a package's
install folder, what that folder holds.

    python scripts/elara.py usage                       # build, then measure
    python scripts/usage.py --bin build/release/bin     # measure these programs
    python scripts/usage.py --bin build/package/stage/bin --stage build/package/stage

The songs are written by this script (self-made, see tests/corpus/README.md):
guitar tracks of eighth-note pairs and quarter notes on varied frets, with
articulations, dynamics and rests, so that files compress as real ones do.
Only the standard library is used; counters come from the operating system
(Windows: GetProcessMemoryInfo, GetProcessTimes, GetProcessIoCounters; elsewhere
wait4's rusage, where the peak is the resident set and private bytes are not
reported).
"""
from __future__ import annotations

import argparse
import json
import os
import platform
import statistics
import subprocess
import sys
import tempfile
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WINDOWS = os.name == "nt"
EXE = ".exe" if WINDOWS else ""
MB = 1024 * 1024

# ---------------------------------------------------------------------------
# Process counters


@dataclass
class Run:
    exit_code: int
    wall: float                      # seconds
    cpu: float                       # seconds of processor time, user and kernel
    peak_ws: float                   # MB: peak working set (resident set elsewhere)
    peak_private: float | None       # MB: peak committed private bytes (Windows)
    read: float | None               # MB read (all I/O on Windows, block input elsewhere)
    written: float | None            # MB written
    hold: dict[str, float] = field(default_factory=dict)  # a window while the app held
    output: str = ""


if WINDOWS:
    import ctypes
    import ctypes.wintypes as wt

    class _MemoryCounters(ctypes.Structure):
        _fields_ = [("cb", wt.DWORD), ("PageFaultCount", wt.DWORD),
                    ("PeakWorkingSetSize", ctypes.c_size_t), ("WorkingSetSize", ctypes.c_size_t),
                    ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                    ("QuotaPagedPoolUsage", ctypes.c_size_t),
                    ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                    ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                    ("PagefileUsage", ctypes.c_size_t), ("PeakPagefileUsage", ctypes.c_size_t),
                    ("PrivateUsage", ctypes.c_size_t)]

    class _IoCounters(ctypes.Structure):
        _fields_ = [(name, ctypes.c_ulonglong) for name in (
            "ReadOperationCount", "WriteOperationCount", "OtherOperationCount",
            "ReadTransferCount", "WriteTransferCount", "OtherTransferCount")]

    _psapi = ctypes.WinDLL("psapi")
    _kernel32 = ctypes.WinDLL("kernel32")

    def _memory(handle: int) -> _MemoryCounters:
        counters = _MemoryCounters()
        counters.cb = ctypes.sizeof(counters)
        _psapi.GetProcessMemoryInfo(wt.HANDLE(handle), ctypes.byref(counters), counters.cb)
        return counters

    def _cpu(handle: int) -> float:
        times = [wt.FILETIME() for _ in range(4)]
        _kernel32.GetProcessTimes(wt.HANDLE(handle), *[ctypes.byref(t) for t in times])
        return sum((t.dwHighDateTime << 32 | t.dwLowDateTime) for t in times[2:]) / 1e7

    def _io(handle: int) -> _IoCounters:
        counters = _IoCounters()
        _kernel32.GetProcessIoCounters(wt.HANDLE(handle), ctypes.byref(counters))
        return counters


def _reader(stream, lines: list[str], hold_seen: threading.Event, ready: list[float]) -> None:
    for raw in iter(stream.readline, b""):
        line = raw.decode("utf-8", "replace")
        lines.append(line)
        if line.startswith("hold:") and not hold_seen.is_set():
            ready.append(time.perf_counter())
            hold_seen.set()
    stream.close()


def measure(cmd: list[str], *, env: dict[str, str] | None = None, hold: float = 0.0,
            timeout: float = 900.0) -> Run:
    """Runs cmd to its end. With hold, the app prints `hold: N s` once it is
    ready and stays open N seconds: the processor time and memory between that
    line and a second before its end are reported apart."""
    lines: list[str] = []
    hold_seen = threading.Event()
    ready: list[float] = []
    start = time.perf_counter()
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
    reader = threading.Thread(target=_reader, args=(proc.stdout, lines, hold_seen, ready),
                              daemon=True)
    reader.start()
    window: dict[str, float] = {}
    # Not proc.poll() elsewhere: it would reap the child before wait4 reads its usage.
    if hold > 0 and hold_seen.wait(timeout) and (not WINDOWS or proc.poll() is None):
        span = max(hold - 1.0, 0.5)
        if WINDOWS:
            handle = int(proc._handle)  # noqa: SLF001 - the process handle Popen holds
            cpu0, t0 = _cpu(handle), time.perf_counter()
            time.sleep(span)
            cpu1, t1 = _cpu(handle), time.perf_counter()
            memory = _memory(handle)
            window = {"cpu_percent": 100.0 * (cpu1 - cpu0) / (t1 - t0),
                      "ws": memory.WorkingSetSize / MB, "private": memory.PrivateUsage / MB}
        else:
            time.sleep(span)
        window["ready_s"] = ready[0] - start
    if WINDOWS:
        try:
            proc.wait(timeout)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
        wall = time.perf_counter() - start
        handle = int(proc._handle)  # noqa: SLF001
        memory, io = _memory(handle), _io(handle)
        run = Run(proc.returncode, wall, _cpu(handle), memory.PeakWorkingSetSize / MB,
                  memory.PeakPagefileUsage / MB, io.ReadTransferCount / MB,
                  io.WriteTransferCount / MB, window)
    else:
        _, status, usage = os.wait4(proc.pid, 0)
        wall = time.perf_counter() - start
        proc.returncode = os.waitstatus_to_exitcode(status)
        peak = usage.ru_maxrss / (MB if sys.platform == "darwin" else 1024)
        run = Run(proc.returncode, wall, usage.ru_utime + usage.ru_stime, peak, None,
                  usage.ru_inblock * 512 / MB, usage.ru_oublock * 512 / MB, window)
    reader.join(5)
    run.output = "".join(lines)
    return run


# ---------------------------------------------------------------------------
# Songs

_TUNING = (40, 45, 50, 55, 59, 64)  # E2 A2 D3 G3 B3 E4, string 6 to string 1
_STEPS = (("C", 0), ("C", 1), ("D", 0), ("D", 1), ("E", 0), ("F", 0), ("F", 1), ("G", 0),
          ("G", 1), ("A", 0), ("A", 1), ("B", 0))


def _pitch(midi: int) -> str:
    step, alter = _STEPS[midi % 12]
    alter_text = f"<alter>{alter}</alter>" if alter else ""
    return f"<pitch><step>{step}</step>{alter_text}<octave>{midi // 12 - 1}</octave></pitch>"


def _note(string: int, fret: int, duration: int, kind: str, *, chord: bool = False,
          extra: str = "") -> str:
    midi = _TUNING[6 - string] + fret
    return (f"<note>{'<chord/>' if chord else ''}{_pitch(midi)}<duration>{duration}</duration>"
            f"<voice>1</voice><type>{kind}</type><notations>{extra}<technical>"
            f"<string>{string}</string><fret>{fret}</fret></technical></notations></note>")


def write_song(path: Path, tracks: int, measures: int) -> int:
    """Writes a MusicXML song of guitar tracks; returns its number of notes."""
    seed = 12345

    def rand(n: int) -> int:
        nonlocal seed
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        return (seed >> 16) % n

    tuning = "".join(
        f'<staff-tuning line="{line}"><tuning-step>{_STEPS[m % 12][0]}</tuning-step>'
        + (f"<tuning-alter>{_STEPS[m % 12][1]}</tuning-alter>" if _STEPS[m % 12][1] else "")
        + f"<tuning-octave>{m // 12 - 1}</tuning-octave></staff-tuning>"
        for line, m in enumerate(_TUNING, start=1))
    dynamics = ("p", "mp", "mf", "f")
    notes = 0
    with path.open("w", encoding="utf-8", newline="\n") as out:
        out.write('<?xml version="1.0" encoding="UTF-8"?>\n<score-partwise version="4.0">\n'
                  "<work><work-title>Usage Song</work-title></work>\n<part-list>\n")
        for t in range(tracks):
            out.write(f'<score-part id="P{t + 1}"><part-name>Guitar {t + 1}</part-name>'
                      f"</score-part>\n")
        out.write("</part-list>\n")
        for t in range(tracks):
            out.write(f'<part id="P{t + 1}">\n')
            for m in range(measures):
                out.write(f'<measure number="{m + 1}">')
                if m == 0:
                    out.write("<attributes><divisions>2</divisions><key><fifths>0</fifths></key>"
                              "<time><beats>4</beats><beat-type>4</beat-type></time>"
                              "<clef><sign>G</sign><line>2</line>"
                              "<clef-octave-change>-1</clef-octave-change></clef>"
                              f"<staff-details><staff-lines>6</staff-lines>{tuning}"
                              "</staff-details></attributes>")
                    if t == 0:
                        out.write('<direction placement="above"><direction-type><metronome>'
                                  "<beat-unit>quarter</beat-unit><per-minute>120</per-minute>"
                                  '</metronome></direction-type><sound tempo="120"/></direction>')
                if m % 8 == 0:
                    out.write('<direction placement="below"><direction-type><dynamics>'
                              f"<{dynamics[(m // 8 + t) % 4]}/></dynamics></direction-type>"
                              "</direction>")
                shape = (m + t) % 4
                if shape == 3 and rand(4) == 0:
                    out.write("<note><rest/><duration>8</duration><voice>1</voice>"
                              "<type>whole</type></note>")
                elif shape == 2:
                    for _ in range(4):
                        string = 1 + rand(6)
                        extra = "<articulations><accent/></articulations>" if rand(5) == 0 else ""
                        out.write(_note(string, rand(13), 2, "quarter", extra=extra))
                        notes += 1
                else:
                    for _ in range(8):
                        low = 4 + rand(3)
                        fret = rand(13)
                        extra = ("<articulations><staccato/></articulations>"
                                 if rand(6) == 0 else "")
                        out.write(_note(low, fret, 1, "eighth", extra=extra))
                        out.write(_note(low - 2, min(fret + 2, 15), 1, "eighth", chord=True))
                        notes += 2
                out.write("</measure>\n")
            out.write("</part>\n")
        out.write("</score-partwise>\n")
    return notes


# ---------------------------------------------------------------------------
# Scenarios


@dataclass
class Scenario:
    name: str
    cmd: list[str]
    output: Path | None = None    # a file the run writes, sized after it
    hold: float = 0.0
    app: bool = False


def _median(values: list[float | None]) -> float | None:
    present = [v for v in values if v is not None]
    return statistics.median(present) if present else None


def run_scenarios(scenarios: list[Scenario], runs: int, env: dict[str, str]) -> list[dict]:
    results = []
    for scenario in scenarios:
        samples: list[Run] = []
        size = None
        for _ in range(runs):
            if scenario.output is not None and scenario.output.exists():
                scenario.output.unlink()
            run = measure(scenario.cmd, env=env, hold=scenario.hold)
            if run.exit_code != 0:
                print(f"[usage] {scenario.name}: exit {run.exit_code}\n{run.output[-2000:]}",
                      file=sys.stderr)
                break
            samples.append(run)
            if scenario.output is not None and scenario.output.exists():
                size = scenario.output.stat().st_size / MB
        if not samples:
            results.append({"name": scenario.name, "failed": True})
            continue
        row = {
            "name": scenario.name,
            "runs": len(samples),
            "wall_s": _median([r.wall for r in samples]),
            "cpu_s": _median([r.cpu for r in samples]),
            "peak_ws_mb": _median([r.peak_ws for r in samples]),
            "peak_private_mb": _median([r.peak_private for r in samples]),
            "read_mb": _median([r.read for r in samples]),
            "written_mb": _median([r.written for r in samples]),
            "output_mb": size,
        }
        if scenario.hold:
            for key in ("ready_s", "cpu_percent", "ws", "private"):
                row[f"hold_{key}"] = _median([r.hold.get(key) for r in samples])
        results.append(row)
        print(f"[usage] {_line(row)}", flush=True)
    return results


def _fmt(value: float | None, digits: int = 1) -> str:
    return "-" if value is None else f"{value:.{digits}f}"


def _line(row: dict) -> str:
    if row.get("failed"):
        return f"{row['name']}: failed"
    text = (f"{row['name']}: {_fmt(row['wall_s'], 2)} s, cpu {_fmt(row['cpu_s'], 2)} s, "
            f"peak {_fmt(row['peak_ws_mb'])} MB ws / {_fmt(row['peak_private_mb'])} MB private, "
            f"read {_fmt(row['read_mb'])} MB, wrote {_fmt(row['written_mb'])} MB")
    if row.get("output_mb") is not None:
        text += f", file {_fmt(row['output_mb'], 2)} MB"
    if "hold_cpu_percent" in row:
        text += (f"; ready after {_fmt(row['hold_ready_s'], 2)} s, held: "
                 f"cpu {_fmt(row['hold_cpu_percent'], 2)} % of a core, "
                 f"{_fmt(row['hold_ws'])} MB ws / {_fmt(row['hold_private'])} MB private")
    return text


def markdown(results: list[dict]) -> str:
    head = ("| Scenario | Wall s | CPU s | Peak working set MB | Peak private MB | Read MB "
            "| Written MB | File MB | Held: ready s, CPU % of a core, WS MB, private MB |\n"
            "|---|---|---|---|---|---|---|---|---|\n")
    rows = []
    for row in results:
        if row.get("failed"):
            rows.append(f"| {row['name']} | failed |||||||| ")
            continue
        held = ""
        if "hold_cpu_percent" in row:
            held = (f"{_fmt(row['hold_ready_s'], 2)}, {_fmt(row['hold_cpu_percent'], 2)}, "
                    f"{_fmt(row['hold_ws'])}, "
                    f"{_fmt(row['hold_private'])}")
        rows.append(f"| {row['name']} | {_fmt(row['wall_s'], 2)} | {_fmt(row['cpu_s'], 2)} | "
                    f"{_fmt(row['peak_ws_mb'])} | {_fmt(row['peak_private_mb'])} | "
                    f"{_fmt(row['read_mb'])} | {_fmt(row['written_mb'])} | "
                    f"{_fmt(row['output_mb'], 2)} | {held} |")
    return head + "\n".join(rows) + "\n"


# ---------------------------------------------------------------------------
# Sizes


def _tree_size(path: Path) -> int:
    return sum(f.stat().st_size for f in path.rglob("*") if f.is_file())


def _stage_part(relative: str) -> str:
    name = relative.lower()
    base = name.rsplit("/", 1)[-1]
    if "/soundfonts/" in name or name.endswith((".sf2", ".sf3")):
        return "SoundFont"
    if "/sfz/" in name:
        return "guitar SFZ"
    if base in (f"elara{EXE}", f"elara-cli{EXE}"):
        return "Elara programs"
    if "/fonts/" in name:
        return "fonts for the CLI"
    if name.startswith(("qml/", "lib/qml/")) or "/qml/" in name:
        return "Qt QML modules"
    if name.startswith("plugins/") or "/plugins/" in name:
        return "Qt plugins"
    if base.startswith(("qt6", "libqt6")) or "qt6" in base:
        return "Qt libraries"
    if base.startswith(("d3dcompiler", "dxcompiler", "dxil", "opengl32sw")):
        return "graphics shader compilers"
    if base.startswith(("msvcp", "vcruntime", "concrt", "vccorlib")):
        return "C++ runtime"
    if base.endswith((".dll", ".so", ".dylib")) or ".so." in base:
        return "other libraries"
    if "translations/" in name:
        return "Qt translations"
    return "other"


def sizes(bin_dir: Path, stage: Path | None) -> dict:
    report: dict = {"programs": {}}
    for name in (f"elara{EXE}", f"elara-cli{EXE}"):
        path = bin_dir / name
        if path.exists():
            report["programs"][name] = path.stat().st_size / MB
    if stage is not None and stage.is_dir():
        parts: dict[str, float] = {}
        files: list[tuple[float, str]] = []
        for f in stage.rglob("*"):
            if f.is_file():
                relative = f.relative_to(stage).as_posix()
                size = f.stat().st_size / MB
                parts[_stage_part(relative)] = parts.get(_stage_part(relative), 0.0) + size
                files.append((size, relative))
        report["stage_mb"] = sum(parts.values())
        report["stage_files"] = len(files)
        report["stage_parts"] = dict(sorted(parts.items(), key=lambda kv: -kv[1]))
        report["largest"] = [(round(s, 2), n) for s, n in sorted(files, reverse=True)[:25]]
        archives = sorted(stage.parent.glob("Elara-*.zip")) + sorted(stage.parent.glob("*.msi"))
        report["archives"] = {a.name: a.stat().st_size / MB for a in archives}
    return report


# ---------------------------------------------------------------------------


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--bin", type=Path, default=ROOT / "build" / "release" / "bin",
                        help="folder holding elara and elara-cli")
    parser.add_argument("--stage", type=Path, help="a package's install folder, to size it")
    parser.add_argument("--work", type=Path, help="folder for the songs and outputs "
                        "(default: a temporary one)")
    parser.add_argument("--tracks", type=int, default=20)
    parser.add_argument("--measures", type=int, default=500,
                        help="measures of the large song the app opens")
    parser.add_argument("--huge-measures", type=int, default=1500,
                        help="measures of the song the CLI works on")
    parser.add_argument("--runs", type=int, default=3, help="runs per scenario (median)")
    parser.add_argument("--hold", type=float, default=10.0,
                        help="seconds the app stays open for the idle and playback scenarios")
    parser.add_argument("--only", help="run only scenarios whose name contains this text")
    parser.add_argument("--no-app", action="store_true", help="measure the CLI only")
    parser.add_argument("--offscreen", action="store_true",
                        help="run the app without a screen (Qt's offscreen platform)")
    parser.add_argument("--json", type=Path, help="also write the results here")
    args = parser.parse_args(argv)

    bin_dir = args.bin.resolve()
    cli = bin_dir / f"elara-cli{EXE}"
    app = bin_dir / f"elara{EXE}"
    if not cli.exists():
        print(f"[usage] {cli} is missing: build it first (elara.py usage does)", file=sys.stderr)
        return 1
    env = dict(os.environ)
    qt_root = env.get("QT_ROOT_DIR")
    if WINDOWS and qt_root:
        env["PATH"] = env.get("PATH", "") + os.pathsep + str(Path(qt_root) / "bin")
    if args.offscreen:
        env["QT_QPA_PLATFORM"] = "offscreen"

    temporary = None
    if args.work is None:
        temporary = tempfile.TemporaryDirectory(prefix="elara-usage-")
        work = Path(temporary.name)
    else:
        work = args.work.resolve()
        work.mkdir(parents=True, exist_ok=True)

    report: dict = {"machine": {"system": platform.platform(), "processor": platform.processor(),
                                "cpus": os.cpu_count(), "python": platform.python_version()},
                    "bin": str(bin_dir)}
    try:
        large_xml = work / "large.musicxml"
        huge_xml = work / "huge.musicxml"
        report["songs"] = {
            "large": {"tracks": args.tracks, "measures": args.measures,
                      "notes": write_song(large_xml, args.tracks, args.measures),
                      "musicxml_mb": large_xml.stat().st_size / MB},
            "huge": {"tracks": args.tracks, "measures": args.huge_measures,
                     "notes": write_song(huge_xml, args.tracks, args.huge_measures),
                     "musicxml_mb": huge_xml.stat().st_size / MB},
        }
        large, huge = work / "large.elara", work / "huge.elara"
        for xml, song in ((large_xml, large), (huge_xml, huge)):
            done = subprocess.run([str(cli), "import", str(xml), "--out", str(song), "--force"],
                                  env=env, capture_output=True, text=True)
            if done.returncode != 0:
                print(done.stdout + done.stderr, file=sys.stderr)
                return 1
        huge_mid = work / "huge.mid"
        subprocess.run([str(cli), "export", str(huge), str(huge_mid), "--force"], env=env,
                       check=True, capture_output=True)
        for key, song in (("large", large), ("huge", huge)):
            report["songs"][key]["elara_mb"] = song.stat().st_size / MB

        out = work / "out"
        out.mkdir(exist_ok=True)
        c = str(cli)
        transposes = [f"transpose {1 if i % 2 == 0 else -1}" for i in range(20)]
        scenarios = [
            Scenario("cli: version (start and quit)", [c, "--json", "version"]),
            Scenario("cli: import huge MusicXML, save .elara",
                     [c, "import", str(huge_xml), "--out", str(out / "i.elara"), "--force"],
                     out / "i.elara"),
            Scenario("cli: import huge MIDI", [c, "import", str(huge_mid)]),
            Scenario("cli: open huge .elara, save again",
                     [c, "edit", "--in", str(huge), "--out", str(out / "s.elara")],
                     out / "s.elara"),
            Scenario("cli: transpose huge song 20 times",
                     [c, "edit", "--in", str(huge), *transposes]),
            Scenario("cli: export huge as MusicXML",
                     [c, "export", str(huge), str(out / "e.musicxml"), "--force"],
                     out / "e.musicxml"),
            Scenario("cli: export huge as MXL",
                     [c, "export", str(huge), str(out / "e.mxl"), "--force"], out / "e.mxl"),
            Scenario("cli: export huge as MIDI",
                     [c, "export", str(huge), str(out / "e.mid"), "--force"], out / "e.mid"),
            Scenario("cli: sequence huge", [c, "sequence", str(huge)]),
            Scenario("cli: render large to audio (built-in synth)",
                     [c, "render-audio", str(large)]),
            Scenario("cli: one track of large as PDF pages",
                     [c, "render", str(large), "--layout", "page", "--pdf",
                      str(out / "t.pdf")], out / "t.pdf"),
        ]
        share = bin_dir.parent / "share" / "elara"
        soundfonts = sorted((share / "soundfonts").glob("*.sf[23]")) if share.is_dir() else []
        sfzs = sorted((share / "sfz").rglob("*.sfz")) if share.is_dir() else []
        if soundfonts:
            scenarios.append(Scenario("cli: render large to audio (included SoundFont)",
                                      [c, "render-audio", str(large), "--soundfont",
                                       str(soundfonts[0])]))
        if sfzs:
            scenarios.append(Scenario("cli: read the included guitar SFZ",
                                      [c, "sfz", str(sfzs[0])]))
        if not args.no_app and app.exists():
            a = str(app)
            size = ["--size", "1280x860"]
            shot = str(out / "shot.png")
            scenarios += [
                Scenario("app: start, show the welcome pane, quit",
                         [a, *size, "--screenshot", shot], app=True),
                Scenario("app: open large song, first track", [a, str(large), *size,
                                                               "--screenshot", shot], app=True),
                Scenario("app: open large song, all tracks",
                         [a, str(large), *size, "--track", "all", "--screenshot", shot],
                         app=True),
                Scenario("app: open large song, continuous view",
                         [a, str(large), *size, "--continuous", "--screenshot", shot],
                         app=True),
                Scenario("app: smoke-test session on large song (edits, views, PDF, export)",
                         [a, str(large), *size, "--smoke-test"], app=True),
            ]
            if "--hold" in subprocess.run([a, "--help"], env=env, capture_output=True,
                                          text=True).stdout:
                hold = f"{args.hold:g}"
                scenarios += [
                    Scenario("app: welcome pane, idle", [a, *size, "--hold", hold],
                             hold=args.hold, app=True),
                    Scenario("app: large song open and idle", [a, str(large), *size, "--hold",
                                                               hold], hold=args.hold, app=True),
                    Scenario("app: large song playing (built-in synth)",
                             [a, str(large), *size, "--hold", hold, "--play"], hold=args.hold,
                             app=True),
                ]
                if soundfonts:
                    scenarios.append(Scenario(
                        "app: large song playing (included SoundFont)",
                        [a, str(large), *size, "--hold", hold, "--play", "--included-soundfont"],
                        hold=args.hold, app=True))
                if sfzs:
                    scenarios.append(Scenario(
                        "app: large song playing (included guitar SFZ)",
                        [a, str(large), *size, "--hold", hold, "--play",
                         "--included-guitar-sfz"], hold=args.hold, app=True))
        if args.only:
            scenarios = [s for s in scenarios if args.only in s.name]
        report["results"] = run_scenarios(scenarios, args.runs, env)
        report["sizes"] = sizes(bin_dir, args.stage.resolve() if args.stage else None)
    finally:
        if temporary is not None:
            temporary.cleanup()

    print()
    print("Songs: " + "; ".join(
        f"{k} {v['tracks']} tracks x {v['measures']} measures, {v['notes']} notes, "
        f"{_fmt(v['musicxml_mb'])} MB MusicXML, {_fmt(v['elara_mb'], 2)} MB .elara"
        for k, v in report["songs"].items()))
    print()
    print(markdown(report["results"]))
    sz = report["sizes"]
    print("Programs: " + ", ".join(f"{k} {_fmt(v, 2)} MB" for k, v in sz["programs"].items()))
    if "stage_mb" in sz:
        print(f"Install folder: {_fmt(sz['stage_mb'])} MB in {sz['stage_files']} files; "
              + ", ".join(f"{k} {_fmt(v)} MB" for k, v in sz["stage_parts"].items()))
        if sz["archives"]:
            print("Archives: " + ", ".join(f"{k} {_fmt(v)} MB" for k, v in sz["archives"].items()))
    if args.json:
        args.json.write_text(json.dumps(report, indent=2), encoding="utf-8")
    return 1 if any(r.get("failed") for r in report["results"]) else 0


if __name__ == "__main__":
    sys.exit(main())
