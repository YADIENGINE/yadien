"""set_developer_password: the record holds a PBKDF2 hash the launcher can check, never the password."""

import hashlib
import unittest

from Scripts.set_developer_password import ALGORITHM, ITERATIONS, make_record


class RecordTests(unittest.TestCase):
    def test_record_matches_the_launchers_check(self):
        salt = bytes(range(16))
        record = make_record("correct horse", salt, 100_000)
        # Same reference value as the C++ test (Tests/CryptoTests.cpp)
        self.assertEqual(record["hash"], "57f2c2f0739748d516419b062a884666323c583ea4ae165504a81f7b53c62a09")
        self.assertEqual(record["salt"], salt.hex())
        self.assertEqual(record["algorithm"], ALGORITHM)
        self.assertNotIn("correct horse", str(record))

    def test_default_cost_is_strong(self):
        self.assertGreaterEqual(ITERATIONS, 600_000)
        self.assertEqual(len(make_record("x" * 8, b"s" * 16)["hash"]), 2 * hashlib.sha256().digest_size)


if __name__ == "__main__":
    unittest.main()
