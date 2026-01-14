import torch
from datasets import load_dataset
from unsloth import FastLanguageModel
from transformers import TrainingArguments
from trl import SFTTrainer

# --- Configuration ---
# Qwen 2.5 Coder 7B Instruct (SOTA late 2025)
MODEL_NAME = "Qwen/Qwen2.5-Coder-7B-Instruct"
NEW_MODEL_NAME = "qwen2.5-coder-7b-deluge-agentic"
DATASET_FILE = "deluge_agentic_data.jsonl"
OUTPUT_DIR = "./results"

# --- Training Parameters & Memory Tuning ---
# Reduced to 16k for T4 GPU (16GB VRAM) compatibility.
# While samples are longer, T4 cannot handle 40k.
MAX_SEQ_LENGTH = 16384
LORA_R = 16
LORA_ALPHA = 16
# Batch size MUST be 1 to fit 40k tokens in 24GB VRAM (L4)
BATCH_SIZE = 1
# Increase grad accumulation to maintain effective batch size
GRAD_ACCUMULATION_STEPS = 8

# --- Load Model with Unsloth ---
model, tokenizer = FastLanguageModel.from_pretrained(
    model_name=MODEL_NAME,
    max_seq_length=MAX_SEQ_LENGTH,
    dtype=None,
    load_in_4bit=True,
)

# --- LoRA Config ---
model = FastLanguageModel.get_peft_model(
    model,
    r=LORA_R,
    target_modules=[
        "q_proj",
        "k_proj",
        "v_proj",
        "o_proj",
        "gate_proj",
        "up_proj",
        "down_proj",
    ],
    lora_alpha=LORA_ALPHA,
    lora_dropout=0,
    bias="none",
    use_gradient_checkpointing="unsloth",
    random_state=3407,
)

# --- Dataset & Formatting ---
# Load only 10 samples for testing purposes
dataset = load_dataset("json", data_files=DATASET_FILE, split="train").select(range(10))


def format_prompt(sample):
    if sample.get("input"):
        user_content = f"{sample['instruction']}\n\n{sample['input']}"
    else:
        user_content = sample["instruction"]

    # Simple, robust Qwen ChatML format
    prompt = (
        f"<|im_start|>system\nYou are an intelligent coding assistant for the Deluge Firmware.<|im_end|>\n"
        f"<|im_start|>user\n{user_content}<|im_end|>\n"
        f"<|im_start|>assistant\n{sample['output']}<|im_end|>"
    )

    return {"text": prompt}


formatted_dataset = dataset.map(format_prompt)

# Print stats to verify context usage
avg_len = sum(len(tokenizer.encode(x["text"])) for x in formatted_dataset) / len(
    formatted_dataset
)
print(f"Average Token Length: {avg_len:.0f}")

# --- Training Arguments ---
training_args = TrainingArguments(
    output_dir=OUTPUT_DIR,
    per_device_train_batch_size=BATCH_SIZE,
    gradient_accumulation_steps=GRAD_ACCUMULATION_STEPS,
    warmup_steps=10,
    # 3 epochs is standard, roughly 80 steps total with this config
    num_train_epochs=3,
    learning_rate=2e-4,
    fp16=not torch.cuda.is_bf16_supported(),
    bf16=torch.cuda.is_bf16_supported(),
    logging_steps=1,
    optim="adamw_8bit",
    weight_decay=0.01,
    lr_scheduler_type="cosine",
    seed=3407,
)

# --- Trainer ---
trainer = SFTTrainer(
    model=model,
    tokenizer=tokenizer,
    train_dataset=formatted_dataset,
    dataset_text_field="text",
    max_seq_length=MAX_SEQ_LENGTH,
    dataset_num_proc=2,
    packing=False,  # Important: False for long complex samples
    args=training_args,
)

print(f"Starting training on {MODEL_NAME} with context {MAX_SEQ_LENGTH}...")
trainer_stats = trainer.train()

print(f"Saving model to {NEW_MODEL_NAME}...")
model.save_pretrained(NEW_MODEL_NAME)
tokenizer.save_pretrained(NEW_MODEL_NAME)
print("Done!")
