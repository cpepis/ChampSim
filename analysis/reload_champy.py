import importlib
import ChamPy
import ChamPy.parser
import ChamPy.stats
import ChamPy.plots


def reload():
    importlib.reload(ChamPy.parser)
    importlib.reload(ChamPy.stats)
    importlib.reload(ChamPy.plots)
    importlib.reload(ChamPy)
    print("✅ Reloaded ChamPy and submodules.")
