# Automated CodeLlama/Qwen Training on GCP

This guide details how to use the `run_gcp_training.sh` automation script to fine-tune 7B LLMs (CodeLlama, Qwen 2.5 Coder) on Google Cloud Platform.

## 1. Prerequisites

### Install Google Cloud CLI
If you haven't installed `gcloud` yet, run the included helper script:
```bash
chmod +x install_gcloud.sh
./install_gcloud.sh
gcloud init
```
*Follow the on-screen prompts to log in and select your project.*

## 2. The Automation Script (`run_gcp_training.sh`)

We have developed a robust automation script that handles the entire lifecycle of the training job.

### Features
*   **Auto-Healing**: Loops through 10+ GCP zones (`us-central1`, `us-west1`, `us-east1`, etc.) to find available GPU stock.
*   **Cost & Safety**:
    *   Uses **Spot Instances** (60-90% cheaper).
    *   **Auto-Deletes** the VM immediately after training (success or failure) to prevent accidental billing.
*   **Hardware**: Defaults to **NVIDIA T4** (16GB VRAM) which allows for easier quota access than A100/L4.
*   **Stack**: Automatically installs **Unsloth**, PyTorch, and dependencies on a fresh Deep Learning VM.

### Usage
Simply run:
```bash
./run_gcp_training.sh
```

The script will:
1.  Find an available zone.
2.  Create a VM.
3.  Upload your data (`deluge_agentic_data.jsonl`) and code.
4.  Run the training.
5.  Download the trained adapter to `./qwen2.5-coder-7b-deluge-agentic`.
6.  **Delete the VM**.

## 3. Troubleshooting & Quotas

### "Quota Exceeded" Errors
The most common error is `Quota 'NVIDIA_T4_GPUS' exceeded. Limit: 0.0`.
New GCP accounts often start with **0 GPUs**. You must manually request an increase.

**How to Fix:**
1.  Go to the [**GCP Quotas Page**](https://console.cloud.google.com/iam-admin/quotas).
2.  Filter by:
    *   **Service**: Compute Engine API
    *   **Dimensions**: `region: us-central1` (or whichever region you prefer, e.g., `us-west1`)
    *   **Quota**: `NVIDIA T4 GPUs` (committed) OR `Preemptible NVIDIA T4 GPUs` (for Spot).
3.  Select the quota and click **Edit Quotas**.
4.  Request a limit of **1**.
5.  *Reason*: "Deep Learning API Training".
6.  Wait for email approval (usually minutes).

### "Zone Resource Pool Exhausted"
If a specific zone is out of stock, the script will print `Failed in [zone]. Retrying next...` and move on.
*   **Solution**: Let the script run. It checks over 15 zones.

### Training Errors (OOM)
The script uses a conservative context length (`MAX_SEQ_LENGTH = 16384`) to fit on the 16GB T4 GPU.
*   If you get CUDA Out of Memory errors, edit `train_codellama.py` and reduce `MAX_SEQ_LENGTH` (e.g., to `8192`).

## 4. Customization

*   **Switch to L4 GPU (Faster):**
    If you get approved for L4 quotas, edit `run_gcp_training.sh`:
    ```bash
    MACHINE_TYPE="g2-standard-4"
    # Remove ACCELERATOR line (L4 is built-in)
    ```
    And update `train_codellama.py` to allow `MAX_SEQ_LENGTH = 40960`.

*   **Disable Spot Instances:**
    If you need a guaranteed instance that won't be preempted, remove `--provisioning-model=SPOT` from `run_gcp_training.sh`.
