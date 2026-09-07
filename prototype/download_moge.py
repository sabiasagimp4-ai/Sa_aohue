"""Download pinned MIT MoGe-2 ViT-B (normal) weights using the OS trust store (no TLS bypass)."""
import hashlib,json,urllib.request
from pathlib import Path
root=Path(__file__).resolve().parents[1]/'models/moge-2-vitb-normal'
revision='ca5f0e07ff01d3e5a364c1d954ed12ee1814b368'
root.mkdir(parents=True,exist_ok=True)
hashes={}
for name in ['model.pt','README.md']:
    path=root/name
    if not path.exists():
        temp=path.with_suffix(path.suffix+'.part')
        urllib.request.urlretrieve(f'https://huggingface.co/Ruicheng/moge-2-vitb-normal/resolve/{revision}/{name}',temp)
        temp.replace(path)
    hashes[name]=hashlib.sha256(path.read_bytes()).hexdigest()
(root/'revision.txt').write_text(revision)
(root/'sha256.json').write_text(json.dumps(hashes,indent=2));print(root)
