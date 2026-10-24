#!/usr/bin/env python3

import argparse
import hashlib
import shutil
import stat
import sys
import tempfile
import urllib.error
import urllib.request
import zipfile
from pathlib import Path


RESOURCE_URL = "https://www.dropbox.com/scl/fi/t24oer35xoa0ea8e8mj0y/VulkanXR-resources.zip?rlkey=17osezpoxa588xiomx83b8l0s&st=v7jt1042&dl=1"
RESOURCE_SHA256 = "a8abd3ca24aa0634c773f2a952794796f19953aa3548b7695448e0cc2653592b"
CHUNK_SIZE = 1024 * 1024


def download(destination: Path) -> None:
    request = urllib.request.Request(RESOURCE_URL, headers={"User-Agent": "VulkanXR resource downloader"})
    with urllib.request.urlopen(request) as response, destination.open("wb") as output:
        total = int(response.headers.get("Content-Length", 0))
        downloaded = 0
        while chunk := response.read(CHUNK_SIZE):
            output.write(chunk)
            downloaded += len(chunk)
            if total:
                print(f"\rDownloading resources: {downloaded / total:6.1%}", end="", flush=True)
            else:
                print(f"\rDownloading resources: {downloaded / (1024 * 1024):.1f} MiB", end="", flush=True)
    print()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        while chunk := file.read(CHUNK_SIZE):
            digest.update(chunk)
    return digest.hexdigest()


def validate_archive(archive: zipfile.ZipFile) -> None:
    for entry in archive.infolist():
        path = Path(entry.filename)
        if path.is_absolute() or ".." in path.parts:
            raise RuntimeError(f"Unsafe archive path: {entry.filename}")
        mode = entry.external_attr >> 16
        if stat.S_ISLNK(mode):
            raise RuntimeError(f"Archive contains a symbolic link: {entry.filename}")


def extract(archive_path: Path, project_root: Path, force: bool) -> None:
    resources = project_root / "resources"
    if resources.exists() and not force:
        raise RuntimeError(f"{resources} already exists; pass --force to overwrite its files")

    with tempfile.TemporaryDirectory(prefix="vulkanxr-resources-") as temporary_directory:
        staging = Path(temporary_directory)
        with zipfile.ZipFile(archive_path) as archive:
            validate_archive(archive)
            archive.extractall(staging)

        staged_resources = staging / "resources"
        if not staged_resources.is_dir():
            raise RuntimeError("The archive does not contain the expected resources directory")
        shutil.copytree(staged_resources, resources, dirs_exist_ok=force)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Download and extract VulkanXR example resources")
    parser.add_argument("--archive", type=Path, help="Use a local archive instead of downloading it")
    parser.add_argument("--force", action="store_true", help="Overwrite files in an existing resources directory")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    project_root = Path(__file__).resolve().parents[1]

    try:
        if arguments.archive:
            archive_path = arguments.archive.resolve()
            if not archive_path.is_file():
                raise RuntimeError(f"Archive does not exist: {archive_path}")
            temporary_directory = None
        else:
            temporary_directory = tempfile.TemporaryDirectory(prefix="vulkanxr-download-")
            archive_path = Path(temporary_directory.name) / "VulkanXR-resources.zip"
            download(archive_path)

        actual_sha256 = sha256(archive_path)
        if actual_sha256 != RESOURCE_SHA256:
            raise RuntimeError(f"Resource archive checksum mismatch: expected {RESOURCE_SHA256}, got {actual_sha256}")

        print("Resource archive checksum verified")
        extract(archive_path, project_root, arguments.force)
        print(f"Resources installed in {project_root / 'resources'}")
        return 0
    except (OSError, RuntimeError, urllib.error.URLError, zipfile.BadZipFile) as error:
        print(f"Failed to install resources: {error}", file=sys.stderr)
        return 1
    finally:
        if "temporary_directory" in locals() and temporary_directory is not None:
            temporary_directory.cleanup()


if __name__ == "__main__":
    raise SystemExit(main())
