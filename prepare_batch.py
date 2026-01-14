import csv
import json
import git
from train_set import get_commit_diff, REPO_PATH, INPUT_CSV, setup_repo

# Constants
BATCH_OUTPUT_FILE = "batch_requests.jsonl"
SYSTEM_PROMPT = """You are an expert C++ Firmware Engineer working on the Synthstrom Deluge (Renesas RZ/A1L ARM Cortex-A9).

I will provide a GIT DIFF of a feature implemented by a human developer.
Your task is to Reverse Engineer the "Agent Process" that led to this code.

Your Goal:
1.  **Infer the User Instruction**: What exactly did the user ask for? (High-level but technical).
2.  **Simulate the Thought Process**: Write the internal monologue of a senior engineer *before* they wrote this code.
    -   **Analyze**: Identify the root cause or the feature requirements.
    -   **Locate**: Which files are relevant? Why? (Deduce this from the diff).
    -   **Plan**: What specific functions or logic need changing?
    -   **Constraints**: Mention hardware constraints if relevant.
    -   **CRITICAL RULE**: Do NOT mention "the diff", "the commit", or "the existing changes" in the thought process.
        Pretend you are planning the work from scratch.

--- OUTPUT FORMAT (JSON) ---
{
    "instruction": "The inferred user request",
    "thought_process": "1. **Analysis**: ... \\n2. **Plan**: ... "
}
"""


def generate_batch_file():
    setup_repo()
    repo = git.Repo(REPO_PATH)

    print("Reading commits from CSV...")
    commits = []
    with open(INPUT_CSV, "r") as csvfile:
        reader = csv.DictReader(csvfile)
        for row in reader:
            if "Hash" in row and row["Hash"]:
                commits.append(row["Hash"])

    print(f"Found {len(commits)} commits. Generating batch file...")

    with open(BATCH_OUTPUT_FILE, "w") as f:
        for commit_hash in commits:
            try:
                commit = repo.commit(commit_hash)
                diff_data = get_commit_diff(repo, commit)

                if not diff_data:
                    continue

                user_content = f"""
                --- INPUTS ---
                COMMIT MESSAGE: {diff_data["msg"]}

                DIFF:
                {diff_data["diff"]}
                --------------
                """

                # Vertex AI Batch Request Format
                request_entry = {
                    "request": {
                        "contents": [
                            {
                                "role": "user",
                                "parts": [
                                    {"text": SYSTEM_PROMPT + "\n" + user_content}
                                ],
                            }
                        ]
                    }
                }

                f.write(json.dumps(request_entry) + "\n")

            except Exception as e:
                print(f"Skipping {commit_hash}: {e}")

    print(f"Successfully generated {BATCH_OUTPUT_FILE}")
    print(
        "You can upload this to GCS: gsutil cp batch_requests.jsonl gs://YOUR_BUCKET/"
    )


if __name__ == "__main__":
    generate_batch_file()
