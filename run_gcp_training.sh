#!/bin/bash
set -e


# Configuration
INSTANCE_NAME="qwen-trainer"
MACHINE_TYPE="n1-standard-4"
ACCELERATOR="type=nvidia-tesla-t4,count=1"
IMAGE_FAMILY="pytorch-2-7-cu128-ubuntu-2204-nvidia-570"
IMAGE_PROJECT="deeplearning-platform-release"
DISK_SIZE="100GB"
LOCAL_SCRIPT="train_codellama.py"
LOCAL_DATA="deluge_agentic_data.jsonl"
REMOTE_DIR="/home/wargoth"
MODEL_OUTPUT_DIR="qwen2.5-coder-7b-deluge-agentic"

# Candidate zones for T4 availability (Prioritizing US Central/West/East)
CANDIDATE_ZONES=("us-central1-f" "us-central1-c" "us-west1-b" "us-east1-c" "us-east4-c" "us-central1-b" "us-central1-a")

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${BLUE}=== Starting GCP Training Automation ===${NC}"

# Cleanup function
function cleanup {
    # Check if ZONE is set (i.e., we found/created an instance)
    if [ -n "$ZONE" ]; then
        echo -e "${BLUE}=== Cleaning up... Deleting instance $INSTANCE_NAME in $ZONE... ===${NC}"
        gcloud compute instances delete $INSTANCE_NAME --zone $ZONE --quiet || echo "Instance not found or already deleted."
    fi
}
trap cleanup EXIT

# 1. Check existing or Create
# Check if a RUNNING instance already exists
EXISTING_ZONE=$(gcloud compute instances list --filter="name=($INSTANCE_NAME) AND status=RUNNING" --format="value(zone)" | head -n1)

if [ -n "$EXISTING_ZONE" ]; then
    # gcloud returns full URL (https://.../zones/us-central1-a), we need just the name
    ZONE=$(basename $EXISTING_ZONE)
    echo -e "${GREEN}Running instance $INSTANCE_NAME found in $ZONE.${NC}"
else
    echo -e "${BLUE}No running instance found. Attempting creation in candidate zones...${NC}"
    for TRY_ZONE in "${CANDIDATE_ZONES[@]}"; do
        echo -e "${BLUE}Trying to create in $TRY_ZONE...${NC}"
        # We use set +e here to allow failure without exiting the script during the loop
        set +e
        gcloud compute instances create $INSTANCE_NAME \
            --zone=$TRY_ZONE \
            --machine-type=$MACHINE_TYPE \
            --accelerator=$ACCELERATOR \
            --image-family=$IMAGE_FAMILY \
            --image-project=$IMAGE_PROJECT \
            --boot-disk-size=$DISK_SIZE \
            --boot-disk-type=pd-ssd \
            --metadata="install-nvidia-driver=True" \
            --maintenance-policy=TERMINATE
        CREATE_EXIT_CODE=$?
        set -e

        if [ $CREATE_EXIT_CODE -eq 0 ]; then
            ZONE=$TRY_ZONE
            echo -e "${GREEN}Successfully created in $ZONE!${NC}"
            break
        else
            echo -e "${RED}Failed in $TRY_ZONE. Retrying next...${NC}"
        fi
    done
fi

if [ -z "$ZONE" ]; then
    echo -e "${RED}CRITICAL: Could not create instance in any candidate zone.${NC}"
    exit 1
fi

# 2. Wait for SSH to be ready
echo -e "${BLUE}Waiting 30s for instance to initialize...${NC}"
sleep 30
echo -e "${BLUE}Checking SSH connectivity...${NC}"
until gcloud compute ssh $INSTANCE_NAME --zone $ZONE --command "echo SSH ready"; do
    echo -e "${RED}SSH not ready yet, retrying in 10s...${NC}"
    sleep 10
done
echo -e "${GREEN} Instance is ready!${NC}"

# 3. Upload Files
echo -e "${BLUE}Uploading training script and data...${NC}"
gcloud compute scp $LOCAL_SCRIPT $LOCAL_DATA $INSTANCE_NAME:$REMOTE_DIR --zone $ZONE

# 4. Install Dependencies & Train (Remote Execution)
echo -e "${BLUE}Installing dependencies and starting training on VM...${NC}"
gcloud compute ssh $INSTANCE_NAME --zone $ZONE --command "
    set -e
    # Ensure usage of the correct user dir
    cd $REMOTE_DIR

    echo 'Installing Unsloth and dependencies...'
    # Update pip
    pip install --upgrade pip

    # Install Unsloth (optimized for Colab/Cloud)
    # Using specific unsloth config for PyTorch compatibility if needed, but default is usually fine
    pip install \"unsloth[colab-new] @ git+https://github.com/unslothai/unsloth.git\"
    pip install --no-deps xformers \"trl<0.9.0\" peft accelerate bitsandbytes

    echo 'Starting training...'
    python $LOCAL_SCRIPT
"

# 5. Download Model
echo -e "${BLUE}Training complete. Downloading model adapters...${NC}"
# Remove local dir if it exists to avoid errors
rm -rf ./$MODEL_OUTPUT_DIR
gcloud compute scp --recurse $INSTANCE_NAME:$REMOTE_DIR/$MODEL_OUTPUT_DIR ./ --zone $ZONE

echo -e "${GREEN}=== DONE! Model saved to ./$MODEL_OUTPUT_DIR ===${NC}"
