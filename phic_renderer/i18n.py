from __future__ import annotations

from typing import Any, Dict, Optional


_TRANSLATIONS: Dict[str, Dict[str, str]] = {
    "en": {
        "cui.title": "Mini Phigros Renderer",
        "cui.config": "Config",
        "cui.input": "Input",
        "cui.window": "Window",
        "cui.backend": "Backend",
        "cui.audio": "Audio",
        "cui.assets": "Assets",
        "cui.gameplay": "Gameplay",
        "cui.render": "Render",
        "cui.value.none": "(none)",
        "cui.autoplay": "Autoplay",
        "cui.start_end": "Start/End",
        "cui.on": "on",
        "cui.off": "off",
        "cui.help.hint": "Tip: --save_config writes a commented config template.",
    },
    "zh-CN": {
        "cui.title": "Mini Phigros Renderer",
        "cui.config": "配置",
        "cui.input": "输入",
        "cui.window": "窗口",
        "cui.backend": "后端",
        "cui.audio": "音频",
        "cui.assets": "资源",
        "cui.gameplay": "玩法",
        "cui.render": "渲染",
        "cui.value.none": "(无)",
        "cui.autoplay": "自动游玩",
        "cui.start_end": "起止时间",
        "cui.on": "开",
        "cui.off": "关",
        "cui.help.hint": "提示：--save_config 会导出带注释的配置模板。",
    },
}


def normalize_lang(lang: Optional[str]) -> str:
    if not lang:
        return "en"
    s = str(lang).strip()
    if not s:
        return "en"
    low = s.lower()
    if low in {"zh", "zh-cn", "zh_cn", "cn"}:
        return "zh-CN"
    if low in {"en", "en-us", "en_us", "us"}:
        return "en"
    return s


def tr(lang: str, key: str, default: Optional[str] = None) -> str:
    lng = normalize_lang(lang)
    tbl = _TRANSLATIONS.get(lng) or _TRANSLATIONS.get("en") or {}
    if key in tbl:
        return tbl[key]
    en = _TRANSLATIONS.get("en") or {}
    if key in en:
        return en[key]
    return default if default is not None else key


def pick_lang_from_config(cfg_v2_raw: Optional[Dict[str, Any]]) -> Optional[str]:
    if not isinstance(cfg_v2_raw, dict):
        return None
    ui = cfg_v2_raw.get("ui")
    if not isinstance(ui, dict):
        return None
    v = ui.get("lang")
    if v is None:
        return None
    return str(v)
