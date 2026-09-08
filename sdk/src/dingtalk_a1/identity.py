from dataclasses import dataclass, field
import json
from pathlib import Path
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes


@dataclass(frozen=True)
class Identity:
    did: str
    corp_id: str
    device_secret: str = field(repr=False)
    serial_number: str | None = None
    address: str | None = None
    model: str = "A1_Independent_SDK"
    sdk_version: str = "V2.1.3"

    def __post_init__(self):
        if not self.did or not self.corp_id:
            raise ValueError("did and corp_id are required")
        if len(self.device_secret) != 32 or not self.device_secret.isascii():
            raise ValueError("Use the existing app's 32-character deviceSecret, not raw flash bytes")

    @classmethod
    def load(cls, path: str | Path):
        body = json.loads(Path(path).read_text(encoding="utf-8-sig"))
        def get(*keys, default=None):
            return next((body[k] for k in keys if body.get(k) is not None), default)
        return cls(
            did=str(get("did", "deviceId", default="")),
            corp_id=str(get("corp_id", "corpId", default="")),
            device_secret=get("device_secret", "deviceSecret", default=""),
            serial_number=get("serial_number", "sn"), address=get("address"),
            model=get("model", default="A1_Independent_SDK"),
            sdk_version=get("sdk_version", default="V2.1.3"),
        )

    def token(self, challenge: str) -> str:
        plain = challenge.encode("ascii")
        if len(plain) != 32:
            raise ValueError("Expected 32 ASCII challenge bytes")
        # This IS the client-level deviceSecret. Do not hash it a second time.
        key = self.device_secret[:16].encode("ascii")
        encryptor = Cipher(algorithms.AES(key), modes.CBC(key)).encryptor()
        return (encryptor.update(plain) + encryptor.finalize()).hex()
