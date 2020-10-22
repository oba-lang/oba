#!/bin/bash

pushd doc
hugo
popd

rm -rf docs/*
mv doc/docs/* docs/
