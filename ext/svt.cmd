
git clone https://github.com/AOMediaCodec/SVT-AV1.git

cd SVT-AV1
cd Build/linux

./build.sh release
cd ../..
mkdir -p include/svt-av1
cp Source/API/*.h include/svt-av1

cd ..
