from __future__ import annotations

import os
import re
from dataclasses import dataclass
from typing import Any, Callable, Dict, Iterable, List, Optional, Tuple

import requests
from requests.adapters import HTTPAdapter
from urllib3.util.retry import Retry

USER_AGENT = "ChartProvider/1.0"


def build_requests_session() -> requests.Session:
    s = requests.Session()
    retries = Retry(total=5, backoff_factor=0.4, status_forcelist=[429, 500, 502, 503, 504])
    s.mount("https://", HTTPAdapter(max_retries=retries))
    s.headers.update({"User-Agent": USER_AGENT})
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        s.headers.update({"Authorization": f"Bearer {token}"})
    return s


HTTP = build_requests_session()


@dataclass
class PhiraChart:
    id: int
    name: str
    level: str
    charter: str
    composer: str
    illustrator: str
    description: str
    illustration: str
    preview: str
    file: str
    created: str
    updated: str
    chartUpdated: str

    @staticmethod
    def from_json(d: dict) -> "PhiraChart":
        return PhiraChart(
            id=d.get("id"),
            name=d.get("name", ""),
            level=d.get("level", ""),
            charter=d.get("charter", ""),
            composer=d.get("composer", ""),
            illustrator=d.get("illustrator", ""),
            description=d.get("description", ""),
            illustration=d.get("illustration", ""),
            preview=d.get("preview", ""),
            file=d.get("file", ""),
            created=d.get("created", ""),
            updated=d.get("updated", ""),
            chartUpdated=d.get("chartUpdated", ""),
        )


class PhiraClient:
    BASE = "https://phira.5wyxi.com"

    @staticmethod
    def search(
        *,
        pageNum: int = 28,
        page: int = 1,
        order: str = "-updated",
        division: Optional[str] = None,
        rating_min: Optional[float] = None,
        rating_max: Optional[float] = None,
        keyword: Optional[str] = None,
    ) -> dict:
        params: Dict[str, Any] = {"pageNum": pageNum, "page": page, "order": order}
        if division:
            params["division"] = division
        if rating_min is not None and rating_max is not None:
            rating_min = max(0.0, min(1.0, float(rating_min)))
            rating_max = max(0.0, min(1.0, float(rating_max)))
            params["rating"] = f"{rating_min},{rating_max}"
        if keyword:
            params["search"] = keyword
        url = f"{PhiraClient.BASE}/chart"
        resp = HTTP.get(url, params=params, timeout=20)
        resp.raise_for_status()
        return resp.json()

    @staticmethod
    def get_chart(chart_id: int) -> PhiraChart:
        url = f"{PhiraClient.BASE}/chart/{int(chart_id)}"
        resp = HTTP.get(url, timeout=20)
        resp.raise_for_status()
        return PhiraChart.from_json(resp.json())


class PhigrosClient:
    OWNER = "7aGiven"
    REPO = "Phigros_Resource"

    BRANCHES: Dict[str, str] = {
        "chart": "chart",
        "music": "music",
        "illustration": "illustration",
    }

    @staticmethod
    def github_api(path: str, params: Optional[dict] = None) -> dict:
        url = f"https://api.github.com/repos/{PhigrosClient.OWNER}/{PhigrosClient.REPO}/{path}"
        r = HTTP.get(url, params=params or {}, timeout=30)
        r.raise_for_status()
        return r.json()

    @staticmethod
    def fetch_tree(branch: str) -> List[dict]:
        data = PhigrosClient.github_api(f"git/trees/{branch}", params={"recursive": 1})
        return data.get("tree", [])

    @staticmethod
    def raw_url(branch: str, path: str) -> str:
        return f"https://raw.githubusercontent.com/{PhigrosClient.OWNER}/{PhigrosClient.REPO}/{branch}/{path}"

    SONG_RX = re.compile(r"^([^/]+)\.([^/]+)\.0/([^/]+)\.json$")

    @staticmethod
    def index_charts(tree: List[dict]) -> Dict[str, dict]:
        idx: Dict[str, dict] = {}
        for ent in tree:
            if ent.get("type") != "blob":
                continue
            path = ent.get("path", "")
            m = PhigrosClient.SONG_RX.match(path)
            if not m:
                continue
            song, composer, diff = m.groups()
            base = f"{song}.{composer}"
            d = idx.setdefault(base, {"song": song, "composer": composer, "diffs": [], "paths": {}})
            if diff not in d["diffs"]:
                d["diffs"].append(diff)
            d["paths"][diff] = path

        def diff_key(x: str) -> tuple:
            order = {"EZ": 0, "HD": 1, "IN": 2, "AT": 3, "SP": 4, "EX": 5}
            return (order.get(x.upper(), 99), x.upper())

        for d in idx.values():
            d["diffs"].sort(key=diff_key)
        return idx

    @staticmethod
    def find_asset_path(tree: List[dict], base: str, allowed_exts: Tuple[str, ...]) -> Optional[str]:
        prefix = f"{base}"
        for ent in tree:
            if ent.get("type") != "blob":
                continue
            p = ent.get("path", "")
            if not p.startswith(prefix):
                continue
            ext = os.path.splitext(p)[1].lower()
            if ext in allowed_exts:
                return p
        return None


def safe_filename(name: str) -> str:
    s = str(name)
    s = re.sub(r"[^\w\-\.]+", "_", s)
    s = s.strip("._")
    return s or "file"


def download_file(
    *,
    url: str,
    dest_path: str,
    progress_cb: Optional[Callable[[int], None]] = None,
    session: requests.Session = HTTP,
) -> str:
    with session.get(url, stream=True, timeout=30) as r:
        r.raise_for_status()
        total = int(r.headers.get("Content-Length", 0)) or 0
        dl = 0
        os.makedirs(os.path.dirname(dest_path) or ".", exist_ok=True)
        with open(dest_path, "wb") as f:
            for chunk in r.iter_content(chunk_size=1024 * 64):
                if not chunk:
                    continue
                f.write(chunk)
                dl += len(chunk)
                if total > 0 and progress_cb is not None:
                    progress_cb(int(dl * 100 / total))
    return dest_path


def batch_download(
    jobs: Iterable[Tuple[str, str]],
    *,
    concurrency: int = 4,
    on_item_done: Optional[Callable[[str], None]] = None,
    on_item_error: Optional[Callable[[str, str], None]] = None,
) -> List[str]:
    from concurrent.futures import ThreadPoolExecutor, as_completed

    jobs_l = list(jobs)
    if concurrency <= 0:
        concurrency = 1

    results: List[str] = []

    def _run_one(url: str, dest: str) -> str:
        return download_file(url=url, dest_path=dest)

    with ThreadPoolExecutor(max_workers=int(concurrency)) as ex:
        futs = {ex.submit(_run_one, url, dest): (url, dest) for (url, dest) in jobs_l}
        for fut in as_completed(futs):
            url, dest = futs[fut]
            try:
                p = fut.result()
                results.append(p)
                if on_item_done is not None:
                    on_item_done(p)
            except Exception as e:
                if on_item_error is not None:
                    on_item_error(dest, str(e))

    return results
