// Copyright (c) 2014, Baidu.com, Inc. All Rights Reserved
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Author: yanshiguang02@baidu.com

#ifndef RPC_CLIENT_H_
#define RPC_CLIENT_H_

#include <sofa/pbrpc/pbrpc.h>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include "common/logging.h"
#include "common/mutex.h"
#include "common/thread_pool.h"

namespace galaxy {
namespace ins {
class RpcClient {
 public:
  RpcClient() {
    // 定义 client 对象，一个 client 程序只需要一个 client 对象
    // 可以通过 client_options 指定一些配置参数，譬如线程数、流控等
    sofa::pbrpc::RpcClientOptions options;
    options.max_pending_buffer_size = 10;
    rpc_client_ = new sofa::pbrpc::RpcClient(options);
  }
  ~RpcClient() { delete rpc_client_; }
  template <class T>
  bool GetStub(const std::string server, T** stub) {
    MutexLock lock(&host_map_lock_);
    sofa::pbrpc::RpcChannel* channel = NULL;
    HostMap::iterator it = host_map_.find(server);
    if (it != host_map_.end()) {
      channel = it->second;
    } else {
      // 定义 channel，代表通讯通道，每个服务器地址对应一个 channel
      // 可以通过 channel_options 指定一些配置参数
      sofa::pbrpc::RpcChannelOptions channel_options;
      channel =
          new sofa::pbrpc::RpcChannel(rpc_client_, server, channel_options);
      host_map_[server] = channel;
    }
    *stub = new T(channel);
    return true;
  }
  template <class Stub, class Request, class Response, class Callback>
  bool SendRequest(Stub* stub,
                   void (Stub::*func)(google::protobuf::RpcController*,
                                      const Request*, Response*, Callback*),
                   const Request* request, Response* response,
                   int32_t rpc_timeout, int retry_times) {
    // 定义 controller
    // 用于控制本次调用，并设定超时时间（也可以不设置，缺省为10s）
    sofa::pbrpc::RpcController controller;
    controller.SetTimeout(rpc_timeout * 1000L);
    for (int32_t retry = 0; retry < retry_times; ++retry) {
      (stub->*func)(&controller, request, response, NULL);
      if (controller.Failed()) {
        if (retry < retry_times - 1) {
          LOG(WARNING, "Send failed, retry ...\n");
          usleep(1000000);
        } else {
          LOG(WARNING, "SendRequest fail: %s\n",
              controller.ErrorText().c_str());
        }
      } else {
        return true;
      }
      controller.Reset();
    }
    return false;
  }
  template <class Stub, class Request, class Response, class Callback>
  void AsyncRequest(
      Stub* stub, void (Stub::*func)(google::protobuf::RpcController*,
                                     const Request*, Response*, Callback*),
      const Request* request, Response* response,
      boost::function<void(const Request*, Response*, bool, int)> callback,
      int32_t rpc_timeout, int retry_times) {
    (void)retry_times;
    sofa::pbrpc::RpcController* controller = new sofa::pbrpc::RpcController();
    controller->SetTimeout(rpc_timeout * 1000L);
    google::protobuf::Closure* done = sofa::pbrpc::NewClosure(
        &RpcClient::template RpcCallback<Request, Response, Callback>,
        controller, request, response, callback);
    (stub->*func)(controller, request, response, done);
  }
  template <class Request, class Response, class Callback>
  static void RpcCallback(
      sofa::pbrpc::RpcController* rpc_controller, const Request* request,
      Response* response,
      boost::function<void(const Request*, Response*, bool, int)> callback) {
    bool failed = rpc_controller->Failed();
    int error = rpc_controller->ErrorCode();
    if (failed || error) {
      if (error != sofa::pbrpc::RPC_ERROR_SEND_BUFFER_FULL) {
        LOG(WARNING, "RpcCallback: %s\n", rpc_controller->ErrorText().c_str());
      }
    }
    delete rpc_controller;
    callback(request, response, failed, error);
  }

 private:
  sofa::pbrpc::RpcClient* rpc_client_;
  typedef std::map<std::string, sofa::pbrpc::RpcChannel*> HostMap;
  HostMap host_map_;
  Mutex host_map_lock_;
};

}  // namespace ins
}  // namespace galaxy

#endif  // RPC_CLIENT_H_

/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
