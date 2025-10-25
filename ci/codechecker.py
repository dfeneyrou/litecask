#! /usr/bin/env python3

import os
import sys
import subprocess
import json

# Warning: cplusplus.NewDeleteLeaks has been disabled due to reported doctest.h


def shell(command, check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE):
    return subprocess.run(
        command, stdout=stdout, stderr=stderr, shell=True, universal_newlines=True, check=check)


def process_word(word: str) -> str:
    # If the word starts with "-I" and contains "external"
    if word.startswith("-I") and "external" in word:
        return "-isystem" + word[2:]
    return word


def main():
    # Get all the input for the task
    if [1 for arg in sys.argv if [1 for h in ["-h", "-help", "--help"] if h in arg]]:
        print("Syntax: %s [-h/--help]" % sys.argv[0])
        sys.exit(1)

    sourceDir = shell("git rev-parse --show-toplevel", check=True).stdout.split('\n')[0]
    buildDir = os.path.join(sourceDir, "build", "cc")
    os.makedirs(buildDir, exist_ok=True)
    os.chdir(buildDir)

    # Call CMake to get the JSON with the compilation commands
    ret = shell(
        "cmake -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug %s -DCMAKE_EXPORT_COMPILE_COMMANDS=ON" % sourceDir)
    if ret.returncode != 0:
        print("*** Error while calling cmake:\n%s" % ret.stderr)
        sys.exit(1)

    # Modify a compile_commands.json compilation file and replace the -I instructions with a -isystem one for the
    # external headers, so that the static analysis does not report on them
    with open("%s/compile_commands.json" % buildDir, "r", encoding="utf-8") as fin, \
            open("%s/compile_commands_filtered_headers.json" % buildDir, "w", encoding="utf-8") as fout:
        for line in fin:
            words = line.strip().split()
            processed = [process_word(w) for w in words]
            fout.write(" ".join(processed) + "\n")

    # Call clang-tidy
    ret = shell('CodeChecker analyze --no-missing-checker-error --config "%s/.codechecker_cfg" \
    --skip "%s/.codechecker_skip" --drop-reports-from-skipped-files --output "%s/tmp" \
    --quiet --clean "%s/compile_commands_filtered_headers.json"' % (sourceDir, sourceDir, buildDir, buildDir),
                stdout=None)

    # Prepare the report
    print("Preparing the report")
    with open("%s/tmp/cc-report.json" % buildDir, "w", encoding="utf-8") as fout:
        ret = shell(
            'CodeChecker parse "%s/tmp" --print-steps --trim-path-prefix "%s" --export codeclimate' %
            (buildDir, sourceDir),
            stdout=fout)

    if ret.returncode == 0:
        print("No issue found!")
        return 0
    else:
        print("Report with issues available in file://%s/html/index.html" % buildDir)
        shell(
            'CodeChecker parse --export html "%s/tmp" --output "%s/html" --print-steps --trim-path-prefix "%s"' %
            (buildDir, buildDir, sourceDir),
            stdout=None)


# Bootstrap
if __name__ == "__main__":
    main()
