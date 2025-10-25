#! /usr/bin/env python3

import os
import sys
import subprocess
import json


def shell(command, check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE):
    return subprocess.run(
        command, stdout=stdout, stderr=stderr, shell=True, universal_newlines=True, check=check)


def main():
    # Get all the input for the task
    if [1 for arg in sys.argv if [1 for h in ["-h", "-help", "--help"] if h in arg]]:
        print("Syntax: %s [-h/--help]" % sys.argv[0])
        sys.exit(1)

    sourceDir = shell("git rev-parse --show-toplevel", check=True).stdout.split('\n')[0]
    buildDir = os.path.join(sourceDir, "build", "iwyu")
    os.makedirs(buildDir, exist_ok=True)
    os.chdir(buildDir)

    # Call CMake to get the JSON with the compilation commands
    ret = shell(
        "cmake -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug %s -DCMAKE_EXPORT_COMPILE_COMMANDS=ON" % sourceDir)
    if ret.returncode != 0:
        print("*** Error while calling cmake:\n%s" % ret.stderr)
        sys.exit(1)

    # Filter out the external repository and overwrite the initial compile_commands.json
    filteredJson = [x for x in json.load(open("%s/compile_commands.json" % buildDir))
                    if "external/" not in x["file"]]
    json.dump(filteredJson, open("%s/compile_commands.json" % buildDir, "w"), indent=2)

    # Call clang-tidy
    ret = shell(
        'iwyu_tool -o clang -j "$(nproc --all)" -p "%s/compile_commands.json" -- -Xiwyu --mapping_file=%s/ci/iwyu.imp' %
        (buildDir, sourceDir))

    # Display the status
    errors = [l for l in ret.stdout.split('\n') if ": error:" in l]
    if not errors:
        print("Includes are ok!", "".join([("\n"+l)
              for l in ret.stdout.split('\n') if l.strip() and "are correct" not in l]))
        sys.exit(0)

    for l in errors:
        print(l.replace(sourceDir+"/", ""))
    sys.exit(1)


# Bootstrap
if __name__ == "__main__":
    main()
