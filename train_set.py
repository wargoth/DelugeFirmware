import git
import json
import google.generativeai as genai
import concurrent.futures
import time
from pathlib import Path
import csv
import argparse
import os
from dotenv import load_dotenv

load_dotenv()

# --- CONFIGURATION ---
REPO_URL = "https://github.com/SynthstromAudible/DelugeFirmware.git"
REPO_PATH = "."
OUTPUT_FILE = "deluge_agentic_data.jsonl"
INPUT_CSV = "feature_commits_refined.csv"
# Filter commits to find "Feature Sized" work (not too small, not too huge)
MIN_FILES_CHANGED = 2
MAX_FILES_CHANGED = 8
MAX_DIFF_LINES = 1500


# Configure your Teacher LLM (Gemini Pro recommended for cost/speed)
api_key = os.getenv("GEMINI_API_KEY")
if not api_key:
    raise ValueError("GEMINI_API_KEY not found in .env file or environment variables")

genai.configure(api_key=api_key)


def setup_repo():
    if not Path(REPO_PATH).exists():
        print(f"Cloning {REPO_URL}...")
        git.Repo.clone_from(REPO_URL, REPO_PATH)
    return git.Repo(REPO_PATH)


def get_commit_diff(repo, commit):
    """Extracts the diff and file list from a specific commit."""
    parent = commit.parents[0] if commit.parents else None
    if not parent:
        return None

    diffs = parent.diff(commit, create_patch=True)

    # Filter logic
    if not (MIN_FILES_CHANGED <= len(diffs) <= MAX_FILES_CHANGED):
        return None

    full_diff = ""
    before_context = ""
    file_list = []

    for d in diffs:
        # Handle deleted files (b_path is None) or non-source files
        if not d.b_path or not d.b_path.endswith((".cpp", ".h", ".c", ".hpp")):
            continue

        try:
            diff_text = d.diff.decode("utf-8", errors="ignore")
            full_diff += f"\n--- FILE: {d.b_path} ---\n{diff_text}\n"
            file_list.append(d.b_path)

            # Grab state before
            if d.a_blob:
                try:
                    source = d.a_blob.data_stream.read().decode(
                        "utf-8", errors="ignore"
                    )
                    before_context += (
                        f"\n--- FILE: {d.b_path} (ORIGINAL) ---\n{source}\n"
                    )
                except Exception as e:
                    print(f"Failed to read blob for {d.b_path}: {e}")
            else:
                before_context += f"\n--- FILE: {d.b_path} (NEW FILE) ---\n"

        except Exception as e:
            print(f"Diff error for {d.b_path}: {e}")
            continue

    if len(full_diff.splitlines()) > MAX_DIFF_LINES:
        return None

    return {
        "diff": full_diff,
        "files": file_list,
        "msg": commit.message,
        "before_context": before_context,
    }


def generate_instruction(diff_data):
    """Uses Gemini to reverse-engineer the 'Instruction' from the Code."""

    prompt = f"""
    You are an expert C++ Firmware Engineer working on the Synthstrom Deluge (Renesas RZ/A1L ARM Cortex-A9).

    I will provide a GIT DIFF of a feature implemented by a human developer.
    Your task is to Reverse Engineer the "Agent Process" that led to this code.

    --- INPUTS ---
    COMMIT MESSAGE: {diff_data["msg"]}

    DIFF:
    {diff_data["diff"]}
    --------------

    Your Goal:
    1.  **Infer the User Instruction**: What exactly did the user ask for? (High-level but technical).
    2.  **Simulate the Thought Process**: Write the internal monologue of a senior engineer *before* they wrote this code.
        -   **Analyze**: Identify the root cause or the feature requirements.
        -   **Locate**: Which files are relevant? Why? (Deduce this from the diff).
        -   **Plan**: What specific functions or logic need changing? (e.g. "I need to update `Voice::release` to use a soft fade-out").
        -   **Constraints**: Mention hardware constraints (ARM Cortex-A9, integer math, memory limits) if relevant.
        -   **CRITICAL RULE**: Do NOT mention "the diff", "the commit", or "the existing changes" in the thought process.
            Pretend you are planning the work from scratch based on the User Instruction.
            Use phrasing like "I will modify..." instead of "The code modifies...".
            Use phrasing like "The user wants..." instead of "The commit message says...".

    --- OUTPUT FORMAT (JSON) ---
    {{
        "instruction": "The inferred user request",
        "thought_process": "1. **Analysis**: The user reports a popping sound... \\n2. **Plan**: I need to check `audio_engine.cpp`... "
    }}
    """

    model = genai.GenerativeModel("gemini-3-flash-preview")

    for attempt in range(6):
        try:
            response = model.generate_content(
                prompt, generation_config={"response_mime_type": "application/json"}
            )
            return json.loads(response.text)
        except Exception as e:
            if "429" in str(e):
                wait_time = (2**attempt) + 1
                print(f"Rate limited (429). Retrying in {wait_time}s...")
                time.sleep(wait_time)
                continue
            print(f"LLM Error: {e}")
            return None
    return None


def process_commit(commit_hash):
    """Worker function to process a single commit."""
    try:
        # Create a new local Repo object for thread safety
        repo = git.Repo(REPO_PATH)
        commit = repo.commit(commit_hash)

        diff_data = get_commit_diff(repo, commit)
        if not diff_data:
            return None

        print(
            f"Processing commit: {commit.hexsha[:7]} - {commit.message.strip()[:40]}..."
        )

        # Ask the Teacher LLM
        generated = generate_instruction(diff_data)
        if generated:
            # Handle case where LLM returns a list [ { ... } ]
            if isinstance(generated, list) and len(generated) > 0:
                generated = generated[0]

            if not isinstance(generated, dict):
                print(f"Skipping {commit.hexsha[:7]}: Invalid JSON format (not a dict)")
                return None

            # Format for Unsloth / Alpaca format
            return {
                "instruction": generated.get("instruction", "Unknown"),
                "input": f"Context: This feature modifies {', '.join(diff_data['files'])}.\n\nSTATE BEFORE:\n{diff_data['before_context']}",
                "output": f"<THOUGHT>\n{generated.get('thought_process', 'No thought process provided')}\n</THOUGHT>\n\n<CODE>\n{diff_data['diff']}\n</CODE>",
            }
    except Exception as e:
        print(f"Error processing {commit_hash}: {e}")
        return None


def main():
    # Ensure repo exists
    # If the user overrides repo path via CLI, use that, otherwise use constant (which is ".")
    global REPO_PATH

    parser = argparse.ArgumentParser(
        description="Generate training data from git commits."
    )
    parser.add_argument(
        "--limit",
        "-n",
        type=int,
        help="Limit number of commits to process for testing.",
        default=None,
    )
    parser.add_argument(
        "--repo-path", type=str, default=REPO_PATH, help="Path to the repository."
    )
    args = parser.parse_args()

    REPO_PATH = args.repo_path

    setup_repo()

    print("Reading commits from CSV...")
    commits = []
    with open(INPUT_CSV, "r") as csvfile:
        reader = csv.DictReader(csvfile)
        for row in reader:
            if "Hash" in row and row["Hash"]:
                commits.append(row["Hash"])

    if args.limit:
        print(f"Limiting to first {args.limit} commits.")
        commits = commits[: args.limit]

    print(f"Found {len(commits)} feature commits to process.")
    print("Starting processing with 2 workers (throttled for preview model)...")

    with open(OUTPUT_FILE, "w") as f:
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
            # map logic
            future_to_commit = {executor.submit(process_commit, h): h for h in commits}

            for future in concurrent.futures.as_completed(future_to_commit):
                result = future.result()
                if result:
                    f.write(json.dumps(result) + "\n")
                    f.flush()

    print("Done.")


if __name__ == "__main__":
    main()
