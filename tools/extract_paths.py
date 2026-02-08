from bs4 import BeautifulSoup
import json

in_path = "symbols.html"
out_path = "g_elements.json"
out_path_jsonl = out_path + "l"
with open(in_path, "r", encoding="utf-8", errors="ignore") as f:
    html = f.read()

soup = BeautifulSoup(html, "lxml")
g_elems = soup.find_all("g")

def attrs_dict(tag):
    d = {}
    for k, v in (tag.attrs or {}).items():
        if isinstance(v, list):
            d[k] = " ".join(v)
        else:
            d[k] = v
    return d

out = []
for g in g_elems:
    rec = {
        "id": g.get("id"), 
        "children": []
    }
    # direct children only
    for child in g.find_all(recursive=False):
        if getattr(child, "name", None) is None:
            continue
        # "inherit" attributes from `attrs_dict`
        rec["children"].append({"tag": child.name, **attrs_dict(child)})
    out.append(rec)

with open(out_path, "w", encoding="utf-8") as f:
    json.dump(out, f, ensure_ascii=False, indent=2)

# Also write newline-delimited JSON for streaming tools
with open(out_path_jsonl, "w", encoding="utf-8") as f:
    for rec in out:
        f.write(json.dumps(rec, ensure_ascii=False) + "\n")

print(f"Wrote {len(out)} groups to {out_path} and {out_path_jsonl}")