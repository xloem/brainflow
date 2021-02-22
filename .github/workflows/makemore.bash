#!/usr/bin/env bash

template=run_macos10

for ((a=0; a < 16; a++))
do
    sed 's/^  \([A-Za-z0-9].*\):$/  \1_'"${a}"':/' ${template}.yml > ${template}_${a}.yml
done
