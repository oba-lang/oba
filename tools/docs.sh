#!/bin/bash

set -eu

DOCS_DIR=docs

# Staging directory for docs. This is used instead of a temp-directory so that
# docs can be regenerated and re-rendered without killing the local server.
DOCS_WORKDIR=`pwd`/.docs-staging
mkdir -p $DOCS_WORKDIR

function build {
  workdir=$(mktemp -d)
  cp -r doc $workdir

  echo "info: copied doc/ to $workdir"

  # Generate the example code.
  python3 ./tools/gen_doc_examples.py --content=$workdir/doc/content

  # Build the docs
  pushd $workdir/doc
  hugo
  popd

  # Copy the generated docs to the submodule
  # Make sure we're on master first
  git -C $DOCS_DIR checkout master
  rm -rf $DOCS_DIR/*
  mv $workdir/doc/docs/* $DOCS_DIR/
  rm -rf $workdir
}

function publish {
  build

  pushd $DOCS_DIR
  git add -A
  git commit -m "Regenerate docs"
  git push origin master -f
  popd
}

function serve {
  build

  pushd $DOCS_DIR
  python -m SimpleHTTPServer
  popd
}

while getopts "bps" COMMAND; do
  case $COMMAND in
  b) 
     build
     ;;
  p)
     publish
     ;;
  s) 
     serve
     ;;
  *)
     echo "Invalid option"
     exit 1
     ;;
  esac
done

