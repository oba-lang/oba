#!/bin/bash

set -eu

# Copy the docs for staging
cp -r doc doc-staging

# Generate the example code
python3 ./tools/gen_doc_examples.py --content=doc-staging/content

# Build the docs
pushd doc-staging
hugo
popd

rm -rf docs/*
mv doc-staging/docs/* docs/
rm -rf doc-staging
