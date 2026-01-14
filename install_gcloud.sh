#!/bin/bash
set -e

# Fix: Remove the broken file from the previous attempt if it exists so apt doesn't error out
if [ -f /etc/apt/sources.list.d/google-cloud-sdk.list ]; then
    echo "Removing previous list file to fix 'Malformed entry' error..."
    sudo rm /etc/apt/sources.list.d/google-cloud-sdk.list
fi

echo "Installing prerequisites..."
sudo apt-get update
sudo apt-get install -y apt-transport-https ca-certificates gnupg curl

echo "Importing Google Cloud public key..."
# Using --yes to overwrite if key exists
curl https://packages.cloud.google.com/apt/doc/apt-key.gpg | sudo gpg --dearmor --yes -o /usr/share/keyrings/cloud.google.gpg

echo "Adding Google Cloud SDK distribution URI..."
# FIXED: Replaced incorrect '/' with space between URI and distribution
echo "deb [signed-by=/usr/share/keyrings/cloud.google.gpg] https://packages.cloud.google.com/apt cloud-sdk main" | sudo tee /etc/apt/sources.list.d/google-cloud-sdk.list

echo "Updating and installing google-cloud-cli..."
sudo apt-get update
sudo apt-get install -y google-cloud-cli

echo "Installation complete!"
echo "Run 'gcloud init' to configure your account."
