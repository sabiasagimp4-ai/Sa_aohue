"""Download pinned Apache-2.0 Small weights using the OS trust store (no TLS bypass)."""
import hashlib,json,urllib.request
from pathlib import Path
root=Path(__file__).resolve().parents[1]/'models/depth-anything-v2-small'
revision='5426e4f0f36572d16453bbda7a8389317b1bef99'
root.mkdir(parents=True,exist_ok=True)
hashes={}
for name in ['config.json','preprocessor_config.json','model.safetensors','README.md']:
    path=root/name
    if not path.exists():
        temp=path.with_suffix(path.suffix+'.part')
        urllib.request.urlretrieve(f'https://huggingface.co/depth-anything/Depth-Anything-V2-Small-hf/resolve/{revision}/{name}',temp)
        temp.replace(path)
    hashes[name]=hashlib.sha256(path.read_bytes()).hexdigest()
(root/'revision.txt').write_text(revision)
(root/'sha256.json').write_text(json.dumps(hashes,indent=2));print(root)
