#include <assert.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <signal.h>
#include <sofa/pbrpc/pbrpc.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <vector>
#include "ins_node_impl.h"

DECLARE_string(cluster_members);
DECLARE_int32(ins_port);
DECLARE_int32(server_id);
DECLARE_int32(ins_max_throughput_in);
DECLARE_int32(ins_max_throughput_out);

static volatile bool s_quit = false;
static void SignalIntHandler(int /*sig*/) { s_quit = true; }

int main(int argc, char* argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  SOFA_PBRPC_SET_LOG_LEVEL(WARNING);
  std::string host_name;
  std::vector<std::string> members;
  boost::split(members, FLAGS_cluster_members, boost::is_any_of(","),
               boost::token_compress_on);
  if (members.size() == 0) {
    LOG(FATAL) << "cluster_members is empty, please check your configuration";
  }
  if (FLAGS_server_id < 1 ||
      FLAGS_server_id > static_cast<int32_t>(members.size())) {
    LOG(FATAL) << "bad server_id: " << FLAGS_server_id;
  }
  // offset -> real endpoint
  std::string server_id = members.at(FLAGS_server_id - 1);
  auto* ins_node = new galaxy::ins::InsNodeImpl(server_id, members);
  sofa::pbrpc::RpcServerOptions options;
  options.max_throughput_in = FLAGS_ins_max_throughput_in;
  options.max_throughput_out = FLAGS_ins_max_throughput_out;
  sofa::pbrpc::RpcServer rpc_server(options);
  if (!rpc_server.RegisterService(
           static_cast<galaxy::ins::InsNode*>(ins_node))) {
    LOG(FATAL) << "failed to register ins_node service";
  }

  if (!rpc_server.Start(server_id)) {
    LOG(FATAL) << "failed to start server on " << server_id;
  }
  LOG(INFO) << "Started server on " << server_id;
  signal(SIGINT, SignalIntHandler);
  signal(SIGTERM, SignalIntHandler);
  while (!s_quit) {
    sleep(1);
  }
  rpc_server.Stop();
  LOG(INFO) << "Server shutdown";
  return 0;
}
