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

import sys
import os
import csv
import glob
import platform
import subprocess
import matplotlib
import matplotlib.pyplot as plt
matplotlib.use('Agg')


def shell(command, universal_newlines=True, check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE):
    return subprocess.run(
        command, stdout=stdout, stderr=stderr, shell=True, universal_newlines=universal_newlines, check=check)


def main():
    # Get all the input for the task
    if [1 for arg in sys.argv if [1 for h in ["-h", "-help", "--help"] if h in arg]]:
        print("Syntax: %s [options]" % sys.argv[0])
        print("  -h   this help")
        print("  -l   longer run with more precise data")
        print("  -ll  longest run with even more precise data")
        print("  -n   no run, using data from currently dumped CSV files")
        sys.exit(1)
    longOption = ([arg for arg in sys.argv if arg in ["-l", "-ll"]] + [""])[0]
    doRun = not [1 for arg in sys.argv if arg == "-n"]

    sourceDir = shell("git rev-parse --show-toplevel", check=True).stdout.split('\n')[0]
    print("Entering %s" % os.path.join(sourceDir, "build"))
    os.chdir(os.path.join(sourceDir, "build"))

    # Run benchmark tests
    if doRun:
        print("Running benchmarks")
        ret = shell("./bin/litecask_test -tc='*thread performance' %s" % longOption)
        if ret.returncode != 0:
            print("*** Error while executing the benchmarks:\n%s" % ret.stderr)
            sys.exit(1)

    # Collect results
    Descr, ThreadQty, KeySize, ValueSize, ReadPercent, OperationQty, DurationUs, ForcedWriteSync, CustomValue = range(9)
    data = []
    print("Collecting result files")
    for csv_file in glob.glob("benchmark*.csv"):
        print("  %s" % csv_file)
        with open(csv_file, "r") as csv_file:
            csv_reader = csv.reader(csv_file, delimiter=",")
            try:
                skipHeader = next(csv_reader)
            except:
                continue  # Empty file, most probably
            for row in csv_reader:
                descr = row[0]
                threadQty, keySize, valueSize, readPercent, operationQty, durationUs, forcedWriteSync, customValue = [
                    int(float(s)) for s in row[1:]]
                data.append((descr, threadQty, keySize, valueSize, readPercent,
                             operationQty, durationUs, forcedWriteSync, customValue))

    # Collect info on the machine
    print("Collecting info on the machine")
    testSetupDesc = "%s %s" % (platform.system(), platform.release())
    try:
        cp = subprocess.run(["lscpu"], universal_newlines=True, capture_output=True)
        if cp.returncode == 0:
            model = ([l for l in cp.stdout.split("\n") if "Model name" in l] + [""])[0].split(':')[1].strip()
            cpuQty = ([l for l in cp.stdout.split("\n") if "CPU(s)" in l] + [""])[0].split(':')[1].strip()
            L1 = ([l for l in cp.stdout.split("\n") if "L1d" in l] + [""])[0].split(':')[1].split('(')[0].strip()
            L2 = ([l for l in cp.stdout.split("\n") if "L2" in l] + [""])[0].split(':')[1].split('(')[0].strip()
            L3 = ([l for l in cp.stdout.split("\n") if "L3" in l] + [""])[0].split(':')[1].split('(')[0].strip()
            testSetupDesc += "    CPU(%s): %s     L1 / L2 / L3 = %s / %s / %s" % (cpuQty, model, L1, L2, L3)
            print("  %s" % testSetupDesc)
    except Exception as e:
        print("  Unable to collect information on the CPU: %s" % str(e))

    # Configure matplotlib (font sizes)
    print("Creating graphs")
    plt.rcParams['figure.titlesize'] = 'xx-large'
    plt.rcParams['legend.fontsize'] = 'medium'
    plt.rcParams['axes.labelsize'] = 'large'
    plt.rcParams['axes.titlesize'] = 'xx-large'
    plt.rcParams['xtick.labelsize'] = 'small'
    plt.rcParams['ytick.labelsize'] = 'small'
    plt.style.use('seaborn-v0_8-ticks')

    maxYOpQtyMono = max([1]+[float(d[OperationQty]*d[ThreadQty])/max(1, float(d[DurationUs]))
                             for d in data if d[ThreadQty] == 1]) * 1.02
    maxYOpQtyMult = max([1]+[float(d[OperationQty]*d[ThreadQty])/max(1, float(d[DurationUs]))
                             for d in data if d[Descr] == "Multithread"]) * 1.02
    readPercentages = [100, 95, 0]

    # Throughput
    # ==========

    fig, axs = plt.subplots(1, 2, figsize=(15, 6))
    plt.text(x=0.5, y=0.94, s="Litecask throughput - Monothread", fontsize=18,
             ha="center", transform=fig.transFigure, weight='bold')
    plt.text(x=0.5, y=0.88, s="1M entries - 8 bytes key - Values in cache - Zipf-1.0 access distribution",
             fontsize=12, ha="center", transform=fig.transFigure)
    plt.text(x=0.02, y=0.02, s=testSetupDesc, fontsize=8, ha="left", transform=fig.transFigure)

    # Left: Graph Value size impact on operation rate
    ax = axs[0]
    ax.set(xlabel="Value size", ylabel="Mop/s")
    ax.set_title("Operation throughput")
    for readPercent in readPercentages:
        points = [(d[ValueSize], float(d[OperationQty])/max(1, float(d[DurationUs])))
                  for d in data if d[Descr] == "Monothread" and d[KeySize] == 8 and d[ReadPercent] == readPercent]

        ax.plot([x for x, y in points], [y for x, y in points],
                linestyle='-', marker='o', label=("Read %d%%" % readPercent) if readPercent != 0 else "Write 100%")
    ax.set_ylim(bottom=0., top=maxYOpQtyMono)
    ax.locator_params(axis='y', nbins=15)
    ax.grid()
    ax.legend()

    # Right: Graph Value size impact on memory throughput
    ax = axs[1]
    ax.set(xlabel="Value size", ylabel="MB/s")
    ax.set_title("Memory throughput")
    for readPercent in readPercentages:
        points = [(d[ValueSize], float(d[OperationQty])*d[ValueSize]/max(1, float(d[DurationUs])))
                  for d in data if d[Descr] == "Monothread" and d[KeySize] == 8 and d[ReadPercent] == readPercent]
        ax.plot([x for x, y in points], [y for x, y in points],
                linestyle='-', marker='o', label=("Read %d%%" % readPercent) if readPercent != 0 else "Write 100%")
    ax.set_ylim(bottom=0.)
    ax.locator_params(axis='y', nbins=15)
    ax.grid()
    ax.legend()

    plt.subplots_adjust(top=0.8, bottom=0.15, wspace=0.3, hspace=0.30, left=0.05, right=0.95)

    fig.savefig('litecask_benchmark_throughput_monothread.png')
    plt.clf()

    # Key influence
    # =============

    fig, ax = plt.subplots(1, 1, figsize=(8, 6))
    plt.text(x=0.5, y=0.94, s="Litecask throughput - Monothread", fontsize=18,
             ha="center", transform=fig.transFigure, weight='bold')
    plt.text(x=0.5, y=0.88, s="1M entries - 8 bytes values - Values in cache - Zipf-1.0 access distribution",
             fontsize=12, ha="center", transform=fig.transFigure)
    plt.text(x=0.02, y=0.02, s=testSetupDesc, fontsize=8, ha="left", transform=fig.transFigure)

    # Graph Key size impact
    ax.set(xlabel="Key size", ylabel="Mop/s")
    ax.set_title("Varying key size")
    for readPercent in readPercentages:
        points = [(d[KeySize], float(d[OperationQty])/max(1, float(d[DurationUs]))) for d in data if d[Descr]
                  == "Monothread" and d[ValueSize] == 8 and d[ReadPercent] == readPercent]
        ax.plot([x for x, y in points], [y for x, y in points],
                linestyle='-', marker='o', label=("Read %d%%" % readPercent) if readPercent != 0 else "Write 100%")
    ax.set_ylim(bottom=0., top=maxYOpQtyMono)
    ax.locator_params(axis='y', nbins=15)
    ax.grid()
    ax.legend()
    plt.subplots_adjust(top=0.8, bottom=0.15, left=0.1, right=0.9)

    fig.savefig('litecask_benchmark_throughput_keysize.png')
    plt.clf()

    # Thread influence
    # ================

    fig, ax = plt.subplots(1, 1, figsize=(8, 6))
    plt.text(x=0.5, y=0.94, s="Litecask throughput - Multithread", fontsize=18,
             ha="center", transform=fig.transFigure, weight='bold')
    plt.text(
        x=0.5, y=0.88,
        s="1M entries - 8 bytes keys - 256 bytes values - Values in cache - Zipf-1.0 access distribution", fontsize=12,
        ha="center", transform=fig.transFigure)
    plt.text(x=0.02, y=0.02, s=testSetupDesc, fontsize=8, ha="left", transform=fig.transFigure)

    # Graph threading impact on operation qty
    ax.set(xlabel="Thread qty", ylabel="Mop/s")
    ax.set_title("Varying threads")
    for readPercent in readPercentages:
        points = [(d[ThreadQty], float(d[OperationQty]*d[ThreadQty])/max(1, float(d[DurationUs])))
                  for d in data if d[Descr] == "Multithread" and d[ReadPercent] == readPercent]
        ax.plot([x for x, y in points], [y for x, y in points],
                linestyle='-', marker='o', label=("Read %d%%" % readPercent) if readPercent != 0 else "Write 100%")
    ax.set_ylim(bottom=0., top=maxYOpQtyMult)
    ax.locator_params(axis='y', nbins=20)
    ax.grid()
    ax.legend()
    plt.subplots_adjust(top=0.8, bottom=0.15, left=0.1, right=0.9)

    fig.savefig('litecask_benchmark_throughput_multithread.png')
    plt.clf()


# Bootstrap
if __name__ == "__main__":
    main()
