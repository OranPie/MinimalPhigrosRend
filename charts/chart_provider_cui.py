from __future__ import annotations

import argparse
import os
import sys
import time
from typing import Any, Dict, List, Tuple

from .chart_provider_core import (
    PhigrosClient,
    PhiraClient,
    batch_download,
    download_file,
    safe_filename,
)


def _print(s: str) -> None:
    sys.stdout.write(s + "\n")


def _tty_ok() -> bool:
    try:
        return bool(sys.stdin.isatty() and sys.stdout.isatty())
    except Exception:
        return False


def _try_import_curses():
    try:
        import curses  # type: ignore

        return curses
    except Exception:
        return None


def _curses_multiselect(
    *,
    title: str,
    items: List[Any],
    render_item: Any,
    initial_query: str = "",
) -> List[int] | None:
    curses = _try_import_curses()
    if curses is None:
        return None

    selected: set[int] = set()
    cursor = 0
    query = initial_query

    def _filtered() -> List[Tuple[int, Any]]:
        if not query:
            return list(enumerate(items))
        q = query.lower()
        out: List[Tuple[int, Any]] = []
        for i, it in enumerate(items):
            s = str(render_item(i, it)).lower()
            if q in s:
                out.append((i, it))
        return out

    def _run(stdscr):
        nonlocal cursor, query
        curses.curs_set(0)
        stdscr.nodelay(False)
        stdscr.keypad(True)

        help_line = "UP/DOWN move  SPACE toggle  ENTER confirm  / search  q cancel"

        while True:
            filt = _filtered()
            if cursor >= len(filt):
                cursor = max(0, len(filt) - 1)
            if cursor < 0:
                cursor = 0

            stdscr.erase()
            h, w = stdscr.getmaxyx()

            stdscr.addnstr(0, 0, title, w - 1)
            stdscr.addnstr(1, 0, f"filter: {query}", w - 1)
            stdscr.addnstr(2, 0, help_line, w - 1)

            top = 0
            visible = max(1, h - 4)
            if cursor >= visible:
                top = cursor - visible + 1

            for row in range(visible):
                idx = top + row
                if idx >= len(filt):
                    break
                orig_i, it = filt[idx]
                line = str(render_item(orig_i, it))
                mark = "[x]" if orig_i in selected else "[ ]"
                prefix = f"{mark} "
                y = 3 + row
                if idx == cursor:
                    stdscr.attron(curses.A_REVERSE)
                    stdscr.addnstr(y, 0, (prefix + line)[: max(0, w - 1)], w - 1)
                    stdscr.attroff(curses.A_REVERSE)
                else:
                    stdscr.addnstr(y, 0, (prefix + line)[: max(0, w - 1)], w - 1)

            stdscr.refresh()

            ch = stdscr.getch()
            if ch in (ord("q"), ord("Q")):
                return None
            if ch in (curses.KEY_UP, ord("k")):
                cursor -= 1
                continue
            if ch in (curses.KEY_DOWN, ord("j")):
                cursor += 1
                continue
            if ch == ord(" "):
                if filt:
                    orig_i, _it = filt[cursor]
                    if orig_i in selected:
                        selected.remove(orig_i)
                    else:
                        selected.add(orig_i)
                continue
            if ch in (curses.KEY_ENTER, 10, 13):
                return sorted(selected)
            if ch == ord("/"):
                curses.curs_set(1)
                buf = ""
                while True:
                    stdscr.move(1, min(len("filter: ") + len(buf), max(0, w - 2)))
                    stdscr.refresh()
                    c2 = stdscr.getch()
                    if c2 in (curses.KEY_ENTER, 10, 13):
                        break
                    if c2 in (27,):
                        buf = query
                        break
                    if c2 in (curses.KEY_BACKSPACE, 127, 8):
                        buf = buf[:-1]
                    elif 32 <= c2 <= 126:
                        buf += chr(c2)
                    query = buf
                    cursor = 0
                    stdscr.addnstr(1, 0, f"filter: {query}", w - 1)
                curses.curs_set(0)
                query = buf
                cursor = 0
                continue

    return curses.wrapper(_run)


def _download_jobs_with_status(
    *,
    title: str,
    jobs: List[Tuple[str, str]],
    concurrency: int,
) -> int:
    if not jobs:
        return 0

    curses = _try_import_curses()
    if curses is None or not _tty_ok():
        total = len(jobs)
        done = 0

        def on_done(path: str) -> None:
            nonlocal done
            done += 1
            _print(f"done {done}/{total}: {path}")

        def on_err(dest: str, msg: str) -> None:
            nonlocal done
            done += 1
            _print(f"error {done}/{total}: {dest}: {msg}")

        batch_download(jobs, concurrency=int(concurrency), on_item_done=on_done, on_item_error=on_err)
        return 0

    from concurrent.futures import ThreadPoolExecutor

    total = len(jobs)
    done = 0
    cur_name = ""
    cur_pct = 0
    last_refresh = 0.0

    def _run(stdscr):
        nonlocal done, cur_name, cur_pct, last_refresh
        curses.curs_set(0)
        stdscr.nodelay(True)
        stdscr.keypad(True)

        def _render():
            h, w = stdscr.getmaxyx()
            stdscr.erase()
            stdscr.addnstr(0, 0, title, w - 1)
            stdscr.addnstr(1, 0, f"{done}/{total} done", w - 1)
            stdscr.addnstr(2, 0, f"{cur_pct:3d}%  {cur_name}", w - 1)
            stdscr.addnstr(3, 0, "q to quit UI (downloads continue)", w - 1)
            stdscr.refresh()

        def _download_one(url: str, dest: str) -> None:
            nonlocal cur_name, cur_pct
            cur_name = os.path.basename(dest)
            cur_pct = 0

            def _pcb(p: int) -> None:
                nonlocal cur_pct
                cur_pct = int(p)

            download_file(url=url, dest_path=dest, progress_cb=_pcb)

        with ThreadPoolExecutor(max_workers=max(1, int(concurrency))) as ex:
            futs = [ex.submit(_download_one, url, dest) for (url, dest) in jobs]
            while True:
                alive = 0
                for fut in futs:
                    if fut.done():
                        continue
                    alive += 1
                done = total - alive

                now = time.time()
                if now - last_refresh > 0.05:
                    _render()
                    last_refresh = now

                ch = stdscr.getch()
                if ch in (ord("q"), ord("Q")):
                    stdscr.nodelay(False)
                    stdscr.addnstr(5, 0, "UI quit requested. Waiting for downloads...", 200)
                    stdscr.refresh()
                    break

                if alive == 0:
                    break
                time.sleep(0.02)

            # surface exceptions
            err = None
            for fut in futs:
                try:
                    fut.result()
                except Exception as e:
                    err = e
            if err is not None:
                raise err

    try:
        curses.wrapper(_run)
        return 0
    except Exception as e:
        _print(f"download error: {e}")
        return 2


def cmd_phira_search(args: Any) -> int:
    data = PhiraClient.search(
        pageNum=int(args.page_num),
        page=int(args.page),
        order=str(args.order),
        division=args.division,
        rating_min=args.rating_min,
        rating_max=args.rating_max,
        keyword=args.keyword,
    )

    items = data.get("data") or data.get("items") or []
    if not isinstance(items, list):
        items = []

    for i, it in enumerate(items):
        cid = it.get("id")
        name = it.get("name", "")
        level = it.get("level", "")
        charter = it.get("charter", "")
        composer = it.get("composer", "")
        _print(f"[{i:02d}] id={cid}  {name}  Lv={level}  charter={charter}  composer={composer}")

    return 0


def cmd_phira_pick(args: Any) -> int:
    data = PhiraClient.search(
        pageNum=int(args.page_num),
        page=int(args.page),
        order=str(args.order),
        division=args.division,
        rating_min=args.rating_min,
        rating_max=args.rating_max,
        keyword=args.keyword,
    )

    items = data.get("data") or data.get("items") or []
    if not isinstance(items, list) or not items:
        _print("No results.")
        return 1

    for i, it in enumerate(items):
        cid = it.get("id")
        name = it.get("name", "")
        level = it.get("level", "")
        charter = it.get("charter", "")
        composer = it.get("composer", "")
        _print(f"[{i:02d}] id={cid}  {name}  Lv={level}  charter={charter}  composer={composer}")

    picks: List[int] = []
    if _tty_ok() and not bool(getattr(args, "no_curses", False)):
        sel = _curses_multiselect(
            title="Phira pick",
            items=items,
            render_item=lambda _i, it: f"id={it.get('id')}  {it.get('name','')}  Lv={it.get('level','')}  charter={it.get('charter','')}  composer={it.get('composer','')}",
            initial_query=str(args.keyword or ""),
        )
        if sel is None:
            # canceled or curses unavailable
            pass
        else:
            picks = sel

    if not picks:
        raw = input("Pick indices (e.g. 0,2,5) or blank to cancel: ").strip()
        if not raw:
            return 0

        for part in raw.replace(" ", "").split(","):
            if not part:
                continue
            try:
                picks.append(int(part))
            except:
                pass
        picks = [p for p in picks if 0 <= p < len(items)]
        if not picks:
            _print("No valid picks.")
            return 2

    ids: List[int] = []
    for p in picks:
        try:
            ids.append(int(items[p].get("id")))
        except:
            pass

    if not ids:
        _print("No ids found for picks.")
        return 2

    dest_dir = str(args.dest)
    os.makedirs(dest_dir, exist_ok=True)
    jobs = _resolve_phira_jobs(ids, dest_dir)
    return _download_jobs_with_status(
        title="Downloading Phira charts",
        jobs=jobs,
        concurrency=int(args.concurrency),
    )


def _resolve_phira_jobs(ids: List[int], dest_dir: str) -> List[Tuple[str, str]]:
    jobs: List[Tuple[str, str]] = []
    for cid in ids:
        c = PhiraClient.get_chart(int(cid))
        safe = safe_filename(f"{c.name}.{c.charter}.{c.id}")
        dest = os.path.join(dest_dir, f"{safe}.zip")
        jobs.append((c.file, dest))
    return jobs


def cmd_phira_download(args: Any) -> int:
    dest_dir = str(args.dest)
    os.makedirs(dest_dir, exist_ok=True)

    ids = [int(x) for x in (args.ids or [])]
    if not ids:
        _print("No ids provided.")
        return 2

    jobs = _resolve_phira_jobs(ids, dest_dir)

    total = len(jobs)
    done = 0

    def on_done(path: str) -> None:
        nonlocal done
        done += 1
        _print(f"done {done}/{total}: {path}")

    def on_err(dest: str, msg: str) -> None:
        nonlocal done
        done += 1
        _print(f"error {done}/{total}: {dest}: {msg}")

    batch_download(jobs, concurrency=int(args.concurrency), on_item_done=on_done, on_item_error=on_err)
    return 0


def cmd_phigros_pick(args: Any) -> int:
    branch = str(args.branch)
    dest_dir = str(args.dest)
    os.makedirs(dest_dir, exist_ok=True)

    tree = PhigrosClient.fetch_tree(branch)
    idx = PhigrosClient.index_charts(tree)
    keys = sorted(idx.keys())
    if not keys:
        _print("Empty index.")
        return 1

    q = str(getattr(args, "query", "") or "").strip().lower()
    shown: List[str] = []
    for k in keys:
        if q and (q not in k.lower()):
            continue
        shown.append(k)
        if len(shown) >= int(args.limit):
            break

    if not shown:
        _print("No matches.")
        return 1

    for i, k in enumerate(shown):
        diffs = ",".join(idx[k].get("diffs") or [])
        _print(f"[{i:02d}] {k}  diffs=[{diffs}]")

    bases: List[str] = []
    if _tty_ok() and not bool(getattr(args, "no_curses", False)):
        sel = _curses_multiselect(
            title="Phigros pick",
            items=shown,
            render_item=lambda _i, base: f"{base}  diffs=[{','.join(idx[base].get('diffs') or [])}]",
            initial_query=str(getattr(args, "query", "") or ""),
        )
        if sel is None:
            pass
        else:
            bases = [shown[i] for i in sel if 0 <= i < len(shown)]

    if not bases:
        raw = input("Pick bases by index (e.g. 0,1) or blank to cancel: ").strip()
        if not raw:
            return 0

        picks: List[int] = []
        for part in raw.replace(" ", "").split(","):
            if not part:
                continue
            try:
                picks.append(int(part))
            except:
                pass
        bases = [shown[p] for p in picks if 0 <= p < len(shown)]
        if not bases:
            _print("No valid base picks.")
            return 2

    diff_raw = input("Diffs (e.g. EZ HD IN) blank=all: ").strip()
    diffs = [d.strip() for d in diff_raw.split() if d.strip()] if diff_raw else []

    jobs: List[Tuple[str, str]] = []
    for base in bases:
        d = idx.get(base)
        if not d:
            continue
        chosen = diffs if diffs else list(d.get("diffs") or [])
        for diff in chosen:
            p = (d.get("paths") or {}).get(diff)
            if not p:
                continue
            url = PhigrosClient.raw_url(branch, p)
            dest = os.path.join(dest_dir, f"{safe_filename(base)}.{safe_filename(diff)}.json")
            jobs.append((url, dest))

    if not jobs:
        _print("No jobs.")
        return 1

    return _download_jobs_with_status(
        title="Downloading Phigros charts",
        jobs=jobs,
        concurrency=int(args.concurrency),
    )


def cmd_phigros_index(args: Any) -> int:
    branch = str(args.branch)
    tree = PhigrosClient.fetch_tree(branch)
    idx = PhigrosClient.index_charts(tree)

    keys = sorted(idx.keys())
    for k in keys:
        diffs = ",".join(idx[k].get("diffs") or [])
        _print(f"{k}  diffs=[{diffs}]")
    return 0


def cmd_phigros_download(args: Any) -> int:
    branch = str(args.branch)
    dest_dir = str(args.dest)
    os.makedirs(dest_dir, exist_ok=True)

    tree = PhigrosClient.fetch_tree(branch)
    idx = PhigrosClient.index_charts(tree)

    bases = list(args.base or [])
    if not bases:
        _print("No base keys provided. Use: <Song>.<Composer>")
        return 2

    diffs = [d.strip() for d in (args.diff or []) if d.strip()]

    jobs: List[Tuple[str, str]] = []
    for base in bases:
        if base not in idx:
            _print(f"skip (not found): {base}")
            continue
        d = idx[base]
        chosen = diffs if diffs else list(d.get("diffs") or [])
        for diff in chosen:
            p = (d.get("paths") or {}).get(diff)
            if not p:
                continue
            url = PhigrosClient.raw_url(branch, p)
            dest = os.path.join(dest_dir, f"{safe_filename(base)}.{safe_filename(diff)}.json")
            jobs.append((url, dest))

    total = len(jobs)
    done = 0

    def on_done(path: str) -> None:
        nonlocal done
        done += 1
        _print(f"done {done}/{total}: {path}")

    def on_err(dest: str, msg: str) -> None:
        nonlocal done
        done += 1
        _print(f"error {done}/{total}: {dest}: {msg}")

    batch_download(jobs, concurrency=int(args.concurrency), on_item_done=on_done, on_item_error=on_err)
    return 0


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(prog="chart_provider_cui")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("phira-search", help="Search charts from Phira")
    p.add_argument("--keyword", type=str, default=None)
    p.add_argument("--page", type=int, default=1)
    p.add_argument("--page-num", type=int, default=28)
    p.add_argument("--order", type=str, default="-updated")
    p.add_argument("--division", type=str, default=None)
    p.add_argument("--rating-min", type=float, default=None)
    p.add_argument("--rating-max", type=float, default=None)
    p.set_defaults(func=cmd_phira_search)

    p = sub.add_parser("phira-pick", help="Search Phira then interactively pick and download")
    p.add_argument("--keyword", type=str, default=None)
    p.add_argument("--page", type=int, default=1)
    p.add_argument("--page-num", type=int, default=28)
    p.add_argument("--order", type=str, default="-updated")
    p.add_argument("--division", type=str, default=None)
    p.add_argument("--rating-min", type=float, default=None)
    p.add_argument("--rating-max", type=float, default=None)
    p.add_argument("--dest", type=str, default="charts")
    p.add_argument("--concurrency", type=int, default=4)
    p.add_argument("--no-curses", action="store_true", help="disable curses UI")
    p.set_defaults(func=cmd_phira_pick)

    p = sub.add_parser("phira-download", help="Download Phira charts by IDs")
    p.add_argument("ids", nargs="+", help="chart ids")
    p.add_argument("--dest", type=str, default="charts")
    p.add_argument("--concurrency", type=int, default=4)
    p.set_defaults(func=cmd_phira_download)

    p = sub.add_parser("phigros-index", help="List chart keys from Phigros_Resource")
    p.add_argument("--branch", type=str, default="chart")
    p.set_defaults(func=cmd_phigros_index)

    p = sub.add_parser("phigros-download", help="Download Phigros charts by base key and difficulty")
    p.add_argument("base", nargs="+", help="base key: <Song>.<Composer>")
    p.add_argument("--diff", nargs="*", default=None, help="difficulty list, e.g. EZ HD IN")
    p.add_argument("--branch", type=str, default="chart")
    p.add_argument("--dest", type=str, default="charts")
    p.add_argument("--concurrency", type=int, default=4)
    p.set_defaults(func=cmd_phigros_download)

    p = sub.add_parser("phigros-pick", help="Interactively pick Phigros charts from index and download")
    p.add_argument("--query", type=str, default="", help="filter substring")
    p.add_argument("--limit", type=int, default=50)
    p.add_argument("--branch", type=str, default="chart")
    p.add_argument("--dest", type=str, default="charts")
    p.add_argument("--concurrency", type=int, default=4)
    p.add_argument("--no-curses", action="store_true", help="disable curses UI")
    p.set_defaults(func=cmd_phigros_pick)

    return ap


def main(argv: List[str] | None = None) -> int:
    ap = build_parser()
    args = ap.parse_args(argv)
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
