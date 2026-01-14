# Proposal: Fine-Tuning LLM for Deluge Firmware Coding Agent

## 1. Executive Summary
This proposal outlines the strategy to fine-tune a Large Language Model (LLM), such as CodeGemma or Llama 3, specifically for the DelugeFirmware codebase. By leveraging the project's rich git history, we aim to create a custom coding agent with deep architectural awareness and adherence to the project's specific C++ embedded coding standards.

## 2. Motivation
Standard off-the-shelf LLMs (like GPT-4 or Claude 3.5) are excellent generalist coders but suffer from specific limitations when working in specialized, legacy, or highly custom codebases like `DelugeFirmware`:

*   **Lack of Architectural Context**: Generic models often suggest standard patterns that contradict the specific event-driven or memory-constrained architecture of the firmware.
*   **Style Inconsistency**: They frequently miss project-specific formatting rules, variable naming conventions, and macro usage.
*   **Hallucination of Libraries**: Models may suggest standard C++ libraries (STL) that are either unavailable or banned in the embedded environment due to overhead.

**Goal**: Fine-tune a model to understand *how* features are implemented in *this specific* repository, resulting in higher acceptance rates for generated code and reduced debugging time.

## 3. Methodology: Dataset Generation
We will generate a high-quality instruction-tuning dataset derived directly from the project's git history.

### 3.1. Identifying High-Quality Examples
We have already developed `analyze_features.py` to filter the git log. We will use the resulting `feature_commits_refined.csv` which identifies commits that:
1.  Are actual features (not just typo fixes or maintenance).
2.  Touch a manageable number of files (2-5), representing a logical unit of work.
3.  Have descriptive commit messages which serve as the "Instruction".

### 3.2. Data Extraction Pipeline
We will create a data processing script to iterate through the filtered CSV:
1.  **Context Retrieval**: For each commit, check out the *parent* commit (state before the change).
2.  **Input Construction**: Read the content of the modified files.
3.  **Instruction Pairing**: Pair this code state with the commit message (e.g., "Implement file operations: add copy and move functionality").
4.  **Target Generation**: Check out the *target* commit and read the new state of the files.
5.  **Formatting**: Output JSONL pairs in a standard instruction-tuning format (e.g., ChatML or Alpaca).

**Example Training Sample:**
*   **System**: You are an expert C++ embedded firmware engineer for the Deluge synthesizer.
*   **User**: `[Source Code Context]` Task: Horizontal Menus/ Add the unison submenu to the voice horizontal menu.
*   **Assistant**: `[Updated Source Code]`

## 4. Implementation Strategy (Google Cloud Platform)

### 4.1. Model Selection
*   **Recommended**: **CodeGemma 7B-it** or **Gemma 2 9B**.
*   **Reasoning**: These Google-native open models are state-of-the-art for coding tasks, lightweight enough for cost-effective hosting, and integrate natively with Vertex AI.

### 4.2. Training Infrastructure (Vertex AI)
We will use **Vertex AI Custom Training** jobs.
*   **Technique**: **QLoRA** (Quantized Low-Rank Adaptation). This allows fine-tuning large models on smaller GPUs by freezing most weights and only training adapters.
*   **Hardware**: NVIDIA L4 or A100 GPUs.

## 5. Cost Estimation
*Estimates based on current GCP `us-central1` pricing.*

### 5.1. Data Processing & Storage
*   **Cost**: negligible (< $5.00).
*   Storage of dataset in Google Cloud Storage (GCS).

### 5.2. Training (One-off cost)
Assuming a dataset of ~1,000 high-quality examples (derived from the ~300 features + permutations):
*   **Compute**: Vertex AI Training with 1x NVIDIA A100 (40GB).
*   **Duration**: ~4-8 hours for 2-3 epochs.
*   **Rate**: ~$3.67 per hour (Preemptible/Spot instances are cheaper, ~$1.20/hr).
*   **Total Training Cost**: **$15.00 - $30.00** per run.

### 5.3. Hosting / Inference (Recurring cost)
Once the model is trained, it needs to be hosted to serve requests.

**Option A: Vertex AI Endpoints (On-Demand)**
*   Deploy heavily quantized (4-bit) model to a smaller GPU (NVIDIA L4).
*   **Rate**: ~$0.56 per hour.
*   **Monthly Cost (24/7)**: ~$400/month.
*   *Note: Endpoint can be scaled to 0 when not in use to save costs, known as "Serverless" or auto-scaling.*

**Option B: Local/Consumer GPU (Cheapest for Dev)**
*   Download the LoRA adapters and run locally on a developer machine with an RTX 3090/4090.
*   **Cost**: $0 (Hardware capital cost only).

**Option C: Batch Prediction**
*   If real-time suggestions aren't needed, run a batch job nightly to generate code.
*   **Cost**: Pay only for minutes used.

## 6. Recommendation
1.  **Pilot**: Generate the dataset using the existing scripts.
2.  **Train**: Perform a pilot training run on Vertex AI (~$30).
3.  **Evaluate**: Load the model locally using Ollama or LM Studio to test architectural awareness before committing to cloud hosting costs.

## 7. Next Steps
1.  Approve this proposal.
2.  Create the `generate_training_dataset.py` script to extract code pairs from the git history.
3.  Set up GCP project and bucket for data storage.
