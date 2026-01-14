import csv
import subprocess
import os


def analyze_git_history(repo_path=".", output_file="feature_commits_refined.csv"):
    # Change to repo directory
    try:
        os.chdir(repo_path)
    except FileNotFoundError:
        print(f"Error: Directory '{repo_path}' not found.")
        return

    # Git command to get log with file names
    cmd = [
        "git",
        "log",
        "--pretty=format:COMMIT_START|||%H|||%an|||%ad|||%s",
        "--name-only",
    ]

    try:
        result = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        if result.returncode != 0:
            print(f"Error running git log: {result.stderr}")
            return
    except Exception as e:
        print(f"Failed to execute git command: {e}")
        return

    commits = []
    current_commit = None

    lines = result.stdout.splitlines()
    for line in lines:
        line = line.strip()
        if not line:
            continue

        if line.startswith("COMMIT_START|||"):
            if current_commit:
                commits.append(current_commit)

            parts = line.split("|||")
            current_commit = {
                "hash": parts[1],
                "author": parts[2],
                "date": parts[3],
                "message": parts[4] if len(parts) > 4 else "(no message)",
                "files": [],
            }
        else:
            if current_commit is not None:
                current_commit["files"].append(line)

    if current_commit:
        commits.append(current_commit)

    # Refined Criteria Configuration

    # 1. Exclusion Keywords: Remove maintenance, bugfixes, docs, build tasks
    exclusion_keywords = [
        "fix",
        "bug",
        "resolve",
        "patch",
        "refactor",
        "cleanup",
        "clean up",
        "optimize",
        "perf",
        "performance",
        "doc",
        "docs",
        "document",
        "website",
        "readme",
        "changelog",
        "build",
        "ci",
        "workflow",
        "test",
        "tests",
        "bump",
        "revert",
        "merge",
        "style",
        "lint",
        "formatting",
    ]

    # 2. Path Filtering: Ignore if commit ONLY touches these non-firmware folders
    ignored_prefixes = (
        "website/",
        "docs/",
        ".github/",
        "tests/",
        "test/",
        ".vscode/",
        ".idea/",
        "scripts/",
        "tools/",
    )

    feature_commits = []

    print(f"Total commits processed: {len(commits)}")

    for c in commits:
        num_files = len(c["files"])
        msg_lower = c["message"].lower()

        # Criteria 1: File Count (2-5 files)
        if not (2 <= num_files <= 5):
            continue

        # Criteria 2: Directory Filtering
        # Check if ALL files are in ignored paths
        if c["files"]:
            if all(
                f.startswith(ignored_prefixes)
                or f in [".gitignore", "LICENSE", "start.sh", "CONTRIBUTING.md"]
                for f in c["files"]
            ):
                continue

        # Criteria 3: Blacklist Keywords
        if any(keyword in msg_lower for keyword in exclusion_keywords):
            continue

        feature_commits.append(c)

    print(f"Found {len(feature_commits)} 'feature' commits matching refined criteria.")

    # Write to CSV
    try:
        with open(output_file, "w", newline="", encoding="utf-8") as csvfile:
            fieldnames = ["Hash", "Date", "Author", "Files Changed", "Message"]
            writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
            writer.writeheader()
            for c in feature_commits:
                writer.writerow(
                    {
                        "Hash": c["hash"],
                        "Date": c["date"],
                        "Author": c["author"],
                        "Files Changed": len(c["files"]),
                        "Message": c["message"],
                    }
                )
        print(f"Successfully wrote results to {output_file}")

    except IOError as e:
        print(f"Error writing to CSV: {e}")


if __name__ == "__main__":
    analyze_git_history()
