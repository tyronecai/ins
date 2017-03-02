#! /bin/bash

set -x

script_name=`readlink -f $0`
base_dir=`dirname $script_name`

cd $(base_dir)
rm -rf depends && mkdir -pv depends

cd $base_dir/thirdparty
wget -c https://github.com/google/snappy/releases/download/1.1.4/snappy-1.1.4.tar.gz
tar zxf snappy-1.1.4.tar.gz && cd snappy-1.1.4 && ./configure --enable-shared=no --prefix=$base_dir/depends && make -j4 && make install
rm -rf ../snappy-1.1.4 ../snappy-1.1.4.tar.gz

cd $base_dir/thirdparty
wget -c https://github.com/google/protobuf/releases/download/v2.6.1/protobuf-2.6.1.tar.gz
tar zxf protobuf-2.6.1.tar.gz && cd protobuf-2.6.1 && ./configure --disable-shared --with-pic --prefix $base_dir/depends && make -j4 && make install
rm -rf ../protobuf-2.6.1 ../protobuf-2.6.1.tar.gz

cd $base_dir/thirdparty
wget -c https://github.com/gflags/gflags/archive/v2.2.0.tar.gz
tar xf v2.2.0.tar.gz && cd gflags-2.2.0 && cmake -DGFLAGS_NAMESPACE=google -DCMAKE_CXX_FLAGS=-fPIC -DCMAKE_INSTALL_PREFIX:PATH="$base_dir/depends" && make -j4 && make install
rm -rf ../gflags-2.2.0 ../v2.2.0.tar.gz

cd $base_dir/thirdparty
wget -c https://github.com/google/glog/archive/v0.3.4.tar.gz
tar xf v0.3.4.tar.gz && cd glog-0.3.4 && ./configure --with-pic --prefix=${base_dir}/depends --enable-shared=no && make -j4 && make install
rm -rf ../glog-0.3.4 ../v0.3.4.tar.gz

cd $base_dir/thirdparty
wget -c http://d3dr9sfxru4sde.cloudfront.net/boost_1_61_0.tar.gz
tar xf boost_1_61_0.tar.gz -C ${base_dir}/depends && rm -rf boost_1_61_0.tar.gz

cd $base_dir/thirdparty
git clone https://github.com/baidu/sofa-pbrpc.git sofa-pbrpc
cd sofa-pbrpc && make -e BOOST_HEADER_DIR=${base_dir}/depends/boost_1_61_0 PROTOBUF_DIR=${base_dir}/depends SNAPPY_DIR=${base_dir}/depends OPT="-O2 -Wno-unused-parameter" -j4 && make -e PREFIX=${base_dir}/depends install
rm -rf ../sofa-pbrpc

cd $base_dir
export PATH=$PATH:${base_dir}/depends/bin
make -j4
