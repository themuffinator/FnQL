from __future__ import annotations

import argparse
import os
import sys

from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from fnql_meta import channel_metadata, to_json


def non_negative_int(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be an integer") from exc
    if parsed < 0:
        raise argparse.ArgumentTypeError("must be non-negative")
    return parsed


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="FnQL version metadata helper")
    parser.add_argument(
        "command",
        choices=("summary", "json", "github-env"),
        nargs="?",
        default="summary",
    )
    parser.add_argument("--channel", choices=("release", "manual"), default="release")
    parser.add_argument("--build-number", type=non_negative_int)
    parser.add_argument("--build-date", default=os.environ.get("FNQL_BUILD_DATE"))
    parser.add_argument("--commit", default=os.environ.get("GITHUB_SHA"))
    parser.add_argument("--ref-name", default=os.environ.get("GITHUB_REF_NAME"))
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.build_number is None:
        env_build_number = os.environ.get("FNQL_BUILD_NUMBER")
        if env_build_number:
            try:
                args.build_number = non_negative_int(env_build_number)
            except argparse.ArgumentTypeError as exc:
                print(f"Invalid FNQL_BUILD_NUMBER: {exc}", file=sys.stderr)
                return 2
    try:
        meta = channel_metadata(
            args.channel,
            build_number=args.build_number,
            build_date=args.build_date,
            commit=args.commit,
            ref_name=args.ref_name,
        )
    except ValueError as exc:
        print(f"version.py: {exc}", file=sys.stderr)
        return 2

    if args.command == "json":
        print(to_json(meta))
        return 0

    if args.command == "github-env":
        mapping = {
            "FNQL_PROJECT_NAME": meta["project_name"],
            "FNQL_BASE_VERSION": meta["base_version"],
            "FNQL_VERSION": meta["version"],
            "FNQL_VERSION_LABEL": meta["version_label"],
            "FNQL_RELEASE_TAG": meta["release_tag"],
            "FNQL_RELEASE_TITLE": meta["release_title"],
            "FNQL_ARCHIVE_PREFIX": meta["archive_prefix"],
            "FNQL_BUILD_NUMBER": args.build_number if args.build_number is not None else "",
            "FNQL_BUILD_DATE": meta["build_date"],
            "FNQL_BUILD_DATE_SLUG": meta["build_date_slug"],
            "FNQL_COMMIT": meta["commit"],
            "FNQL_CHANNEL": meta["channel"],
        }
        for key, value in mapping.items():
            print(f"{key}={value}")
        return 0

    print(f"{meta['project_name']} {meta['version_label']} [{meta['channel']}]")
    print(f"tag: {meta['release_tag']}")
    print(f"title: {meta['release_title']}")
    print(f"archives: {meta['archive_prefix']}-<artifact>.zip")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
