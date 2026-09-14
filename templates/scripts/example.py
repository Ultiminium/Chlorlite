# Chlorlite — example embedded-Python gameplay script.
#
# Run with the Python bridge enabled (build the engine with CC_PY=1) and load
# via cc_python_exec_file(eng, "templates/scripts/example.py"), or call a
# specific hook with cc_python_call(eng, "example", "on_score", 1, argv).
#
# The `cc` module is injected by the engine — no import path setup needed.

import cc

# Reserved event channels mirror cc/event.h (game channels start at CC_EVT_USER).
CC_EVT_USER = 1024
EVT_SCORE   = CC_EVT_USER + 0
EVT_LEVELUP = CC_EVT_USER + 1


def on_init():
    """Called once when the script is first loaded."""
    cc.log("example.py loaded at t=%.2f" % cc.time())
    cc.set_var("score", 0.0)
    cc.set_var("level", 1.0)


def add_score(points):
    """Add to the shared score; emit a SCORE event; level up every 100 points."""
    points = float(points)
    score = cc.get_var("score", 0.0) + points
    cc.set_var("score", score)
    cc.emit(EVT_SCORE, int(score), points)   # channel, i=total, f=delta

    level = cc.get_var("level", 1.0)
    if score >= level * 100.0:
        level += 1
        cc.set_var("level", level)
        cc.emit(EVT_LEVELUP, int(level), 0.0)
        cc.log("LEVEL UP -> %d (score %d)" % (int(level), int(score)))
    return score


def greet(name):
    """Demonstrates cc_python_call returning a value to C (as str)."""
    return "hello, %s" % name
