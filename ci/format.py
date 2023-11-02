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
import glob
import subprocess


def shell(command, universal_newlines=True, check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE):
    return subprocess.run(
        command, stdout=stdout, stderr=stderr, shell=True, universal_newlines=universal_newlines, check=check)


def main():
    # Get all the input for the task
    if [1 for arg in sys.argv if [1 for h in ["-h", "-help", "--help"] if h in arg]]:
        print("Syntax: %s [-h/--help] [-v] [nofix]" % sys.argv[0])
        sys.exit(1)
    doFix = not [1 for arg in sys.argv if arg == "nofix"]
    isVerbose = not not [1 for arg in sys.argv if arg == "-v"]

    sourceDir = shell("git rev-parse --show-toplevel", check=True).stdout.split('\n')[0]
    notWellFormattedFiles = []

    # Helper function
    def exclude(fileList):
        exclusion = ['/build/', '/external/']
        return [f for f in fileList if not [1 for e in exclusion if e in f]]

    # Find C++ files and format them
    cppFiles = exclude(glob.glob("%s/**/*.cpp" % sourceDir, recursive=True) +
                       glob.glob("%s/**/*.h" % sourceDir, recursive=True))
    for f in cppFiles:
        ret = shell("clang-format --dry-run --Werror -style=file %s" % f)
        if ret.returncode != 0:
            notWellFormattedFiles.append(f)
            if isVerbose:
                print(ret.stderr)
            if doFix:
                ret = shell("clang-format -i -style=file %s" % f)

    # Find Python files and format them
    pyFiles = exclude(glob.glob("%s/**/*.py" % sourceDir, recursive=True))
    for f in pyFiles:
        ret = shell("autopep8 %s --exit-code --global-config %s/.pycodestyle --experimental %s" %
                    ("--in-place" if doFix else "", sourceDir, f))
        if ret.returncode != 0:
            notWellFormattedFiles.append(f)
            if isVerbose:
                print(ret.stderr)

    # Find CMake files and format them
    cmakeFiles = exclude(glob.glob("%s/**/CMakeLists.txt" % sourceDir, recursive=True))
    for f in cmakeFiles:
        ret = shell("cmake-format --check %s -c %s/.cmake-format.py" % (f, sourceDir))
        if ret.returncode != 0:
            notWellFormattedFiles.append(f)
            if doFix:
                ret = shell("cmake-format %s -i -c %s/.cmake-format.py" % (f, sourceDir))
            if isVerbose:
                print(ret.stderr)

    if notWellFormattedFiles:
        print("Incorrectly formatted files:")
        for f in notWellFormattedFiles:
            print("   %s" % f)
        sys.exit(1)


# Bootstrap
if __name__ == "__main__":
    main()
