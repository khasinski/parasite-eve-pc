"""Search locally decoded dialogue and route results to readable scene names."""
import re
import unicodedata

TAG = re.compile(r"\{[^{}]+\}")
PUNCTUATION = str.maketrans({"’": "'", "‘": "'", "“": '"', "”": '"', "…": "..."})


def readable_dialogue(text):
    # Text continues across control markers and box breaks.
    return " ".join(TAG.sub(lambda match: "Aya" if match[0] == "{name}" else " ", text).split())


def normalize(text):
    return " ".join(unicodedata.normalize("NFKC", text).translate(PUNCTUATION).casefold().split())


def preferred_scenes(scenes):
    """Choose one named view per line, preferring a direct room reference."""
    result = {}
    ranked = sorted(scenes, key=lambda scope: (
        scope != "opera",
        not bool(scenes[scope]["area"]),
        scope.startswith("bank-"),
        scenes[scope]["shared"],
    ))
    for scope in ranked:
        for key in scenes[scope]["keys"]:
            result.setdefault(key, scope)
    return result


def dialogue_matches(index, query):
    needle = normalize(query)
    if not needle:
        return []
    results = []
    for key, (text, searchable) in index.items():
        at = searchable.find(needle)
        if at == -1:
            continue
        start = max(0, at - 32)
        end = min(len(text), max(start + 105, at + len(needle)))
        snippet = ("…" if start else "") + text[start:end] + ("…" if end < len(text) else "")
        results.append((key, snippet))
    return results
