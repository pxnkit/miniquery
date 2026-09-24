import csv
import io
import random
import sqlite3
import subprocess
import sys
import tempfile
from pathlib import Path

exe = str(Path(sys.argv[1]).resolve())
sql = "SELECT customer, SUM(amount_cents) FROM orders WHERE amount_cents >= 100 GROUP BY customer"
rng = random.Random(42)
with tempfile.TemporaryDirectory() as folder:
    path = Path(folder) / "orders.csv"
    rows = [(f"customer_{rng.randrange(17)}", rng.randrange(-500, 10000)) for _ in range(10003)]
    db = sqlite3.connect(":memory:")
    db.execute("CREATE TABLE orders (customer TEXT, amount_cents INTEGER)")
    db.executemany("INSERT INTO orders VALUES (?, ?)", rows)
    expected = dict(db.execute(sql))
    with path.open("w", newline="") as output:
        writer = csv.writer(output)
        writer.writerow(["customer", "amount_cents"])
        writer.writerows(rows)
    for threads in (1, 2, 4, 7):
        run = subprocess.run([exe, str(path), sql, "--threads", str(threads), "--explain"], capture_output=True, text=True, check=True)
        actual = {row["customer"]: int(row["total_cents"]) for row in csv.DictReader(io.StringIO(run.stdout))}
        assert actual == expected, (threads, actual)
        assert "execute_ms=" in run.stderr
    for args in (["--threads", "0"], ["--threads", "65"], ["--threads", "2x"], ["--unknown"]):
        assert subprocess.run([exe, str(path), sql, *args], capture_output=True).returncode != 0
    assert subprocess.run([exe, str(path), "SELECT * FROM orders"], capture_output=True).returncode != 0
    path.write_text("customer,amount_cents\n")
    assert subprocess.check_output([exe, str(path), sql], text=True) == "customer,total_cents\n"
    path.write_text("customer,amount_cents\nalice,not-an-integer\n")
    assert subprocess.run([exe, str(path), sql], capture_output=True).returncode != 0
    path.write_text("customer,amount_cents\nalice,9223372036854775807\nalice,100\n")
    for threads in (1, 2):
        assert subprocess.run([exe, str(path), sql, "--threads", str(threads)], capture_output=True).returncode != 0
print("PASS: SQLite oracle, 4 worker counts, empty input, invalid input and overflow")
