import sys
import csv
import matplotlib.pyplot as plt

num_bins = 200

if __name__ == "__main__":

    if len(sys.argv) != 2:
        print("Usage: python histogram.py inputFile")
        exit(1)

    csvFile = open(sys.argv[1], "r")
    csvFile.readline()  # skip first line

    reader = csv.reader(csvFile, delimiter=',')
    x = []
    for row in reader:
        x.append(int(row[len(row) - 1]))

    print(f'Number of point {len(x)}')
    fig, ax = plt.subplots()
    n, bins, patches = ax.hist(x, num_bins, density=False)
    ax.set_xlabel("Access time [ns]")
    ax.set_ylabel("proportion of cases")
    plt.show()
