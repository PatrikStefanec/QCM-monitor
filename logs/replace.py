import csv

INPUT  = "logs/tm13_alllog_20261117_repeatability_run2.csv"
OUTPUT = "logs/output.csv"
FIND   = "Cu100w"
REPLACE = ""
FIRST_ROW = 1      # 1-based, inclusive
LAST_ROW  = 6500   # 1-based, inclusive
DELIMITER = ","    # change to ";" or "\t" if needed

with open(INPUT, newline="", encoding="utf-8") as fin, \
     open(OUTPUT, "w", newline="", encoding="utf-8") as fout:
    reader = csv.reader(fin, delimiter=DELIMITER)
    writer = csv.writer(fout, delimiter=DELIMITER)

    for row_num, row in enumerate(reader, start=1):
        if FIRST_ROW <= row_num <= LAST_ROW:
            row = [cell.replace(FIND, REPLACE) for cell in row]
        writer.writerow(row)

print("Done ->", OUTPUT)