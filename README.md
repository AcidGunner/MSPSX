# MSPSX
Minesweeper Plus for PlayStation

# How to compile?
1. Install [psn00bsdk](https://github.com/Lameguy64/PSn00bSDK/)
2. Please, make sure $PSN00BSDK_LIBS is a directory with `bin`, `include`, `lib` and etc. folders
3. Compile with this code:
```
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$PSN00BSDK_LIBS/lib/libpsn00b/cmake/sdk.cmake
```

warning the code is ai-assisted
