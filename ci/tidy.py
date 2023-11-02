#! /usr/bin/env python3

# Litecask - High performance, persistent embedded Key-Value storage engine.
#
# The MIT License (MIT)
#
# Copyright(c) 2023, Damien Feneyrou <dfeneyrou@gmail.com>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files(the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions :
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

import os
import sys
import subprocess


def shell(command, universal_newlines=True, check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE):
    return subprocess.run(
        command, stdout=stdout, stderr=stderr, shell=True, universal_newlines=universal_newlines, check=check)


def main():
    # Get all the input for the task
    if [1 for arg in sys.argv if [1 for h in ["-h", "-help", "--help"] if h in arg]]:
        print("Syntax: %s [-h/--help] [fix]" % sys.argv[0])
        sys.exit(1)
    doFix = not not [1 for arg in sys.argv if arg == "fix"]

    compilerName = "clang++"
    sourceDir = shell("git rev-parse --show-toplevel", check=True).stdout.split('\n')[0]
    buildDir = os.path.join(sourceDir, "build", compilerName)
    os.makedirs(buildDir, exist_ok=True)
    os.chdir(buildDir)

    # Call CMake to get the JSON with the compilation commands
    ret = shell(
        "cmake -DCMAKE_CXX_COMPILER=%s -DCMAKE_BUILD_TYPE=Debug %s -DCMAKE_EXPORT_COMPILE_COMMANDS=ON" %
        (compilerName, sourceDir))
    if ret.returncode != 0:
        print("*** Error while calling cmake:\n%s" % ret.stderr)
        sys.exit(1)

    # Call clang-tidy
    ret = shell('run-clang-tidy -quiet -header-filter=".*/(apps|lib)/.*" -p=%s %s' %
                (buildDir, "-fix" if doFix else ""))

    # Success case (good execution and no warning found)
    if ret.returncode == 0:
        sys.exit(0)

    if not ret.stdout:
        # Display execution errors
        print("*** Error while calling clang-tidy:\n%s" % ret.stderr)
    else:
        # Display the found code warnings
        print(ret.stdout)
    sys.exit(1)


# Bootstrap
if __name__ == "__main__":
    main()
