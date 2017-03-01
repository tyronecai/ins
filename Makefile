# OPT ?= -std=c++11 -O2 -DNDEBUG     # (A) Production use (optimized mode)
OPT ?= -std=c++11 -g2 -Wall -fPIC         # (B) Debug mode, w/ full line-level debugging symbols
# OPT ?= std=c++11 -O2 -g2 -DNDEBUG # (C) Profiling mode: opt, but w/debugging symbols

# Thirdparty
DEPENDS=depends
PROTOC=$(DEPENDS)/bin/protoc
BOOST_PATH ?=$(DEPENDS)/boost_1_63_0
NEXUS_LDB_PATH=./thirdparty/leveldb
PREFIX=/usr/local

INCLUDE_PATH = -I. -I$(NEXUS_LDB_PATH)/include -I$(DEPENDS)/include -I$(PREFIX)/include -I$(BOOST_PATH)

LDFLAGS = -L$(DEPENDS)/lib -lsofa-pbrpc -lprotobuf -lsnappy -lgflags -L$(NEXUS_LDB_PATH) -lleveldb \
					-L$(PREFIX)/lib -lrt -lz -lpthread

LDFLAGS_SO = -L$(DEPENDS)/lib \
          -Wl,--whole-archive -lsofa-pbrpc -lprotobuf \
					-Wl,--no-whole-archive -lsnappy \
					-Wl,--whole-archive -lgflags \
					-Wl,--no-whole-archive -L$(NEXUS_LDB_PATH) -lleveldb -L$(PREFIX)/lib -lz -lpthread
CXXFLAGS += $(OPT)

PROTO_FILE = $(wildcard proto/*.proto)
PROTO_SRC = $(patsubst %.proto,%.pb.cc,$(PROTO_FILE))
PROTO_HEADER = $(patsubst %.proto,%.pb.h,$(PROTO_FILE))
PROTO_OBJ = $(patsubst %.proto,%.pb.o,$(PROTO_FILE))

UTIL_SRC = $(filter-out $(wildcard */*test.cc) $(wildcard */*main.cc) server/flags.cc, \
             $(wildcard server/*.cc) $(wildcard storage/*.cc))
UTIL_OBJ = $(patsubst %.cc, %.o, $(UTIL_SRC))
UTIL_HEADER = $(wildcard server/*.h) $(wildcard storage/*.h)

INS_SRC = $(filter-out $(UTIL_SRC) $(wildcard */*test.cc), $(wildcard server/ins_*.cc) \
            $(wildcard storage/*.cc))
INS_OBJ = $(patsubst %.cc, %.o, $(INS_SRC))

INS_CLI_SRC = $(wildcard sdk/ins_*.cc)
INS_CLI_OBJ = $(patsubst %.cc, %.o, $(INS_CLI_SRC))
INS_CLI_HEADER = $(wildcard sdk/*.h)

SAMPLE_SRC = sdk/sample.cc
SAMPLE_OBJ = $(patsubst %.cc, %.o, $(SAMPLE_SRC))
SAMPLE_HEADER = $(wildcard sdk/*.h)

FLAGS_OBJ = $(patsubst %.cc, %.o, $(wildcard server/flags.cc))
COMMON_OBJ = $(patsubst %.cc, %.o, $(wildcard common/*.cc))
OBJS = $(FLAGS_OBJ) $(COMMON_OBJ) $(PROTO_OBJ) $(UTIL_OBJ)
SDK_OBJ = $(patsubst %.cc, %.o, sdk/ins_sdk.cc) $(PROTO_OBJ) $(COMMON_OBJ) $(FLAGS_OBJ)
TEST_SRC = $(wildcard server/*_test.cc) $(wildcard storage/*_test.cc)
TEST_OBJ = $(patsubst %.cc, %.o, $(TEST_SRC))
TESTS = test_binlog test_storage_manager test_user_manager test_performance_center
BIN = ins ins_cli sample
LIB = libins_sdk.a
PY_LIB = libins_py.so

all: $(BIN) cp $(LIB)

nexus_ldb: 
	cd ./thirdparty/leveldb && make
# Depends
$(INS_OBJ) $(INS_CLI_OBJ) $(TEST_OBJ) $(UTIL_OBJ): $(PROTO_HEADER)
$(UTIL_OBJ): $(UTIL_HEADER)
$(INS_CLI_OBJ): $(INS_CLI_HEADER)
$(SAMPLE_OBJ): $(SAMPLE_HEADER)

# Targets
ins: $(INS_OBJ) $(UTIL_OBJS) $(OBJS) nexus_ldb
	$(CXX) $(INS_OBJ) $(UTIL_OBJS) $(OBJS) -o $@ $(LDFLAGS)

ins_cli: $(INS_CLI_OBJ) $(OBJS) nexus_ldb
	$(CXX) $(INS_CLI_OBJ) $(OBJS) -o $@ $(LDFLAGS)

sample: $(SAMPLE_OBJ) $(SDK_OBJ) $(LIB) nexus_ldb
	$(CXX) $(SAMPLE_OBJ) $(LIB) -o $@ $(LDFLAGS)

$(LIB): $(SDK_OBJ)
	ar -rs $@ $(SDK_OBJ)

%.o: %.cc
	$(CXX) $(CXXFLAGS) $(INCLUDE_PATH) -c $< -o $@

%.pb.h %.pb.cc: %.proto
	$(PROTOC) --proto_path=./proto/ --proto_path=/usr/local/include --cpp_out=./proto/ $<

clean:
	rm -rf $(BIN) $(LIB) $(TESTS) $(PY_LIB)
	rm -rf $(INS_OBJ) $(INS_CLI_OBJ) $(SAMPLE_OBJ) $(SDK_OBJ) $(TEST_OBJ) $(UTIL_OBJ)
	rm -rf $(PROTO_SRC) $(PROTO_HEADER)
	rm -rf output/

cp: $(BIN) $(LIB)
	rm -rf output
	mkdir -p output/bin
	mkdir -p output/lib
	mkdir -p output/include
	mv ins output/bin
	mv ins_cli output/bin
	mv sample output/bin
	cp sdk/ins_sdk.h output/include
	mv libins_sdk.a output/lib

sdk: $(LIB)
	mkdir -p output/include
	mkdir -p output/lib
	cp sdk/ins_sdk.h output/include
	cp libins_sdk.a output/lib

python: $(SDK_OBJ) sdk/ins_wrapper.o
	$(CXX) -shared -fPIC -Wl,-soname,$(PY_LIB) -o $(PY_LIB) $(LDFLAGS_SO) $^
	mkdir -p output/python
	cp $(PY_LIB) sdk/ins_sdk.py output/python
	
install: $(LIB)
	cp sdk/ins_sdk.h $(PREFIX)/include
	cp $(LIB) $(PREFIX)/lib

install_sdk: $(LIB)
	cp sdk/ins_sdk.h $(PREFIX)/include
	cp libins_sdk.a $(PREFIX)/lib

.PHONY: test test_binlog test_storage_manager test_user_manager test_performance_center
test: $(TESTS)
	./test_binlog
	./test_storage_manager
	./test_user_manager
	./test_performance_center
	echo "Test done"

test_binlog: storage/binlog_test.o $(UTIL_OBJ) $(OBJS)
	$(CXX) $^ -o $@ $(LDFLAGS) -L$(GTEST_PATH) -lgtest

test_storage_manager: storage/storage_manage_test.o $(UTIL_OBJ) $(OBJS)
	$(CXX) $^ -o $@ $(LDFLAGS) -L$(GTEST_PATH) -lgtest

test_user_manager: server/user_manage_test.o $(UTIL_OBJ) $(OBJS)
	$(CXX) $^ -o $@ $(LDFLAGS) -L$(GTEST_PATH) -lgtest

test_performance_center: server/performance_center_test.o $(UTIL_OBJ) $(OBJS)
	$(CXX) $^ -o $@ $(LDFLAGS) -L$(GTEST_PATH) -lgtest

