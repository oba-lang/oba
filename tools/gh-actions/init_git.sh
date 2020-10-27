#!/bin/bash

# The GH action puts the user name in GITHUB_ACTOR
git config --global user.name $GITHUB_ACTOR
git config --global user.email "ci@oba.com"

# Prepare the SSH private key. The GH action puts it in SSH_KEY.
mkdir $HOME/.ssh
echo "$SSH_KEY" > $HOME/.ssh/id_rsa
chmod 600 $HOME/.ssh/id_rsa

# Update submodules
git submodule update --init
