"""Sets the developer-mode password (Docs/Modes.md): writes DeveloperAccess.json, never the password itself.

Usage, from the launcher repository:
    python Scripts/set_developer_password.py

The record holds a PBKDF2-SHA256 hash with a random salt; the launcher embeds it at build time and checks a typed
password with the same parameters (Core/Developer/DeveloperAccess). The password is a door against opening the
mode by accident, not security: developer-mode work needs access to the engine repository anyway.
"""

import getpass
import hashlib
import json
import secrets
import sys
from pathlib import Path

RECORD_FILE = Path(__file__).resolve().parents[1] / "DeveloperAccess.json"
ALGORITHM = "PBKDF2-SHA256"
ITERATIONS = 600_000
SALT_BYTES = 16
HASH_BYTES = 32
MIN_LENGTH = 8


def make_record(password: str, salt: bytes, iterations: int = ITERATIONS) -> dict:
    digest = hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), salt, iterations, HASH_BYTES)
    return {"format": 1, "algorithm": ALGORITHM, "iterations": iterations, "salt": salt.hex(), "hash": digest.hex()}


def main() -> int:
    password = getpass.getpass("New developer password: ")
    if len(password) < MIN_LENGTH:
        print(f"error: use at least {MIN_LENGTH} characters", file=sys.stderr)
        return 1
    if getpass.getpass("Repeat: ") != password:
        print("error: the passwords differ", file=sys.stderr)
        return 1
    record = make_record(password, secrets.token_bytes(SALT_BYTES))
    RECORD_FILE.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"Wrote {RECORD_FILE.name}; rebuild the launcher to embed it.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
