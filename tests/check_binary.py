"""Windows loader/resource check only; does not start After Effects."""
import ctypes as c
import hashlib,json
from pathlib import Path
p=Path(__file__).resolve().parents[1]/'build/Sa_aohue.aex'
k=c.WinDLL('kernel32',use_last_error=True)
k.LoadLibraryW.argtypes=[c.c_wchar_p];k.LoadLibraryW.restype=c.c_void_p
k.GetProcAddress.argtypes=[c.c_void_p,c.c_char_p];k.GetProcAddress.restype=c.c_void_p
k.FindResourceW.argtypes=[c.c_void_p,c.c_void_p,c.c_wchar_p];k.FindResourceW.restype=c.c_void_p
k.FreeLibrary.argtypes=[c.c_void_p]
h=k.LoadLibraryW(str(p));assert h,c.get_last_error()
try:
    result={'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'EffectMain':bool(k.GetProcAddress(h,b'EffectMain')),'PluginDataEntryFunction2':bool(k.GetProcAddress(h,b'PluginDataEntryFunction2')),'PiPL_16000':bool(k.FindResourceW(h,c.c_void_p(16000),'PiPL'))}
    assert all(result[x] for x in ['EffectMain','PluginDataEntryFunction2','PiPL_16000'])
finally: k.FreeLibrary(h)
(p.parents[1]/'results/binary_check.json').write_text(json.dumps(result,indent=2));print('PASS',result)
