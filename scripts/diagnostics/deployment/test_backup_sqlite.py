import pathlib
import sqlite3
import tempfile
import unittest
from backup_sqlite import backup


class BackupTests(unittest.TestCase):
    def test_committed_snapshot_during_writer_transaction(self):
        with tempfile.TemporaryDirectory(prefix="chronox-backup-test-") as directory:
            root = pathlib.Path(directory)
            source, target = root / "live.db", root / "snapshot.db"
            writer = sqlite3.connect(source)
            try:
                writer.executescript("CREATE TABLE data(value); INSERT INTO data VALUES(7);")
                writer.execute("INSERT INTO data VALUES(9)")  # uncommitted, RESERVED lock
                result = backup(source, target)
                self.assertEqual(result["quick_check"], "ok")
                reader = sqlite3.connect(target)
                try:
                    self.assertEqual(reader.execute("SELECT * FROM data").fetchall(), [(7,)])
                finally:
                    reader.close()
                writer.commit()
                self.assertEqual(writer.execute("SELECT * FROM data").fetchall(), [(7,), (9,)])
            finally:
                writer.close()

    def test_never_overwrites_or_creates_source(self):
        with tempfile.TemporaryDirectory(prefix="chronox-backup-test-") as directory:
            root = pathlib.Path(directory)
            source = root / "source.db"
            sqlite3.connect(source).close()
            target = root / "existing.db"
            target.write_bytes(b"keep")
            with self.assertRaises(FileExistsError):
                backup(source, target)
            self.assertEqual(target.read_bytes(), b"keep")
            with self.assertRaises(FileExistsError):
                backup(source, source)
            with self.assertRaises(FileNotFoundError):
                backup(root / "missing.db", root / "new.db")
            self.assertFalse((root / "missing.db").exists())

    def test_failure_does_not_publish(self):
        with tempfile.TemporaryDirectory(prefix="chronox-backup-test-") as directory:
            root = pathlib.Path(directory)
            source, target = root / "bad.db", root / "snapshot.db"
            source.write_bytes(b"not sqlite")
            with self.assertRaises(sqlite3.DatabaseError):
                backup(source, target)
            self.assertFalse(target.exists())
            self.assertEqual(source.read_bytes(), b"not sqlite")
            self.assertEqual(len(list(root.glob("*.incomplete-*"))), 1)


if __name__ == "__main__":
    unittest.main()
