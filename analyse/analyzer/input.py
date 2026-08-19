from pathlib import Path
from urllib.parse import urlparse
import requests
import hashlib


AUDIO_EXTENSIONS = {
    ".wav",
    ".mp3",
    ".flac",
    ".ogg",
    ".m4a",
    ".aac",
    ".opus",
}


def is_url(value: str) -> bool:
    try:
        parsed = urlparse(value)
        return parsed.scheme in ("http", "https")
    except Exception:
        return False


def download_audio(url: str, temp_dir: Path) -> Path:
    temp_dir.mkdir(parents=True, exist_ok=True)

    url_hash = hashlib.md5(url.encode()).hexdigest()[:12]

    suffix = Path(urlparse(url).path).suffix.lower()

    if suffix not in AUDIO_EXTENSIONS:
        suffix = ".audio"

    output = temp_dir / f"download_{url_hash}{suffix}"

    print(f"[INPUT] Téléchargement : {url}")

    response = requests.get(
        url,
        stream=True,
        timeout=60,
        headers={
            "User-Agent": "Mozilla/5.0 AudioAnalyzer/1.0"
        },
    )

    response.raise_for_status()

    with open(output, "wb") as f:
        for chunk in response.iter_content(chunk_size=1024 * 1024):
            if chunk:
                f.write(chunk)

    print(f"[INPUT] Fichier : {output}")

    return output


def resolve_audio(input_value: str, temp_dir: Path) -> Path:

    if is_url(input_value):
        return download_audio(input_value, temp_dir)

    path = Path(input_value).expanduser().resolve()

    if not path.exists():
        raise FileNotFoundError(
            f"Fichier audio introuvable : {path}"
        )

    if not path.is_file():
        raise ValueError(
            f"Le chemin n'est pas un fichier : {path}"
        )

    return path
