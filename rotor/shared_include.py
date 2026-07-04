# Add the monorepo root to the include path so nodes can #include "shared/telemetry.h".
# DEFERRED (spec §10.2): assumes rotor/ sits beside shared/ at the monorepo root; adjust the
# relative path here if the tree moves during integration.
Import("env")
import os
root = os.path.abspath(os.path.join(env["PROJECT_DIR"], ".."))
env.Append(CPPPATH=[root])
print("[rotor] shared include path: %s" % root)
