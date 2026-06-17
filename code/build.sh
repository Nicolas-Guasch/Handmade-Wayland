#!/bin/bash

# Configuration
SOURCE_FILE="../handmade/code/wayland_handmade.cpp"
BUILD_DIR="../../build"
EXE_NAME="wayland_handmade.exe"
LIBS=""

# Core Flags (Intel Skylake Targeted Optimization)
CLANG_FLAGS="-g -O3 -march=skylake"
CLANG_WARN_FLAGS="-Wall -Wextra -Wno-unused-parameter -Wno-old-style-cast"
#-Werror?

LINK_FLAGS="-flto=thin -fuse-ld=lld"

# Diagnostics for PGO Verification
DIAG_FLAGS="-Wprofile-instr-out-of-date -Wprofile-instr-missing -fdiagnostics-show-hotness -Rpass=inline"

# Error if compiling fails
set -e

# READ Conditional warning argument and set warning flags
CURRENT_WARN=""
if [[ "$2" == "warn" ]]; then
    CURRENT_WARN="$CLANG_WARN_FLAGS"
fi

# Setup directories
mkdir -p "$BUILD_DIR"
pushd "$BUILD_DIR" &>/dev/null

# Read the first argument passed to the script (e.g., ./build.sh debug)
case "$1" in
    "debug")
        echo "Building standard Debug executable..."
        if [[ "$2" == "warn" ]]; then
            echo "--> Warnings ENABLED"
        fi
        clang -g $CURRENT_WARN "$SOURCE_FILE" -o "$EXE_NAME" $LIBS
        ;;
        
    "instrument")
        echo "Building PGO Instrumented executable..."
        clang $CLANG_FLAGS -fprofile-instr-generate "$SOURCE_FILE" -o "$EXE_NAME" $LIBS $LINK_FLAGS
        ;;
        
    "train")
        if [ -f "$EXE_NAME" ]; then
            echo "Launching instrumented game for training..."
            echo "--> Play the game, then exit CLEANLY (close the window) to save data."
            
            # Run the executable locally in the build directory
            ./"$EXE_NAME"
            
            echo "Game closed. Merging raw profile data..."
            if [ -f "default.profraw" ]; then
                llvm-profdata merge -output=game.profdata default.profraw
                echo "Successfully generated game.profdata!"
                rm -f default.profraw # Clean up raw file to avoid stale data next time
            else
                echo "Error: default.profraw not generated. Did the game crash or close unexpectedly?"
                exit 1
            fi
        else
            echo "Error: $EXE_NAME not found! Run './build.sh instrument' first."
            exit 1
        fi
        ;;
        
    "optimize")
        echo "Building final optimized executable with optimized PGO..."
        if [ -f "game.profdata" ]; then
            clang $CLANG_FLAGS -fprofile-use=game.profdata $DIAG_FLAGS "$SOURCE_FILE" -o "$EXE_NAME" $LIBS $LINK_FLAGS
        else
            echo "Error: game.profdata not found! Run './build.sh train' first."
            exit 1
        fi
        ;;
        
    *)
        echo "Usage: ./build.sh [debug [warn] | instrument | train | optimize]"
        exit 1
        ;;
esac

popd &>/dev/null

