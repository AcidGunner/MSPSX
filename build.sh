clear
rm -rf build
mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$PSN00BSDK_LIBS/lib/libpsn00b/cmake/sdk.cmake

make

mv ./mspsx.bin ../
mv ./mspsx.cue ../

cd ..
rm -rf build