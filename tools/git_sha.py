# The build's own identity, as a compiler flag.
#
# RCT_BUILD_SHA lands in the binary, so the running firmware can say which commit it
# was built from. Without it "which build is on the panel" is a question only the
# build machine can answer - and on a panel with two app slots it is not even that:
# which slot the bootloader picks is its own business, so a flash can land in the
# slot nobody boots and the display keeps showing something else. The web interface
# prints the flag next to the version, which turns that question into one lookup.
#
# A dirty tree is marked, because the interesting builds are often the ones that are
# not committed yet - and a build without a git (a tarball, a CI cache) says so
# instead of pretending.
#
# SPDX-License-Identifier: MIT
import subprocess

Import("env")  # noqa: F821 - the SCons environment PlatformIO hands in


def git(*args):
    # The tree PlatformIO builds, not the caller's: a build from a copy that has no
    # git should say so, not name the repository it happens to sit next to.
    try:
        return subprocess.check_output(
            ["git", "-C", env["PROJECT_DIR"], *args],  # noqa: F821
            stderr=subprocess.DEVNULL,
        ).decode("utf-8", "replace").strip()
    except Exception:
        return ""


kurz = git("rev-parse", "--short", "HEAD")
if not kurz:
    kennung = "ohne-git"
else:
    # docs/install is what tools/pack_web_install.sh writes BEFORE it builds: the
    # release folder, the manifests and the release name in devices.json. Counting that
    # as a dirty tree marks every release binary "-dirty" over its own output, and a
    # release that says it was built from an uncommitted tree says nothing.
    if git("status", "--porcelain", "--", ".", ":!docs/install"):
        kurz = kurz + "-dirty"
    kennung = kurz

env.Append(CPPDEFINES=[("RCT_BUILD_SHA", '\\"%s\\"' % kennung)])  # noqa: F821
print("Firmware-Kennung: %s" % kennung)
