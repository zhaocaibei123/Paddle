/* Copyright (c) 2020 PaddlePaddle Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */

#include <ThreadPool.h>
#include <unordered_map>
#include <vector>
#include "Eigen/Dense"
#include "gtest/gtest.h"
#include "paddle/fluid/distributed/common/thread_pool.h"
#include "paddle/fluid/distributed/ps.pb.h"
#include "paddle/fluid/distributed/service/brpc_ps_client.h"
#include "paddle/fluid/distributed/table/common_table.h"
#include "paddle/fluid/distributed/table/table.h"
#include "paddle/fluid/distributed/table/tensor_accessor.h"
#include "paddle/fluid/framework/archive.h"
namespace paddle {
namespace distributed {

TEST(BarrierTable, Barrier) {
  typedef AsyncRequestTask<std::shared_ptr<std::vector<float>>> DenseAsyncTask;
  typedef thread_queue<DenseAsyncTask *, store_value> DenseAsyncTaskQueue;
  std::unordered_map<uint32_t, std::shared_ptr<DenseAsyncTaskQueue>>
      _push_dense_task_queue_map;
  uint64_t merge_size = 100;
  auto push_timer =
      std::make_shared<CostTimer>("pslib_downpour_client_push_dense");
  auto parse_timer =
      std::make_shared<CostTimer>("pslib_downpour_client_push_dense_parse");
  _push_dense_task_queue_map[0] = std::make_shared<DenseAsyncTaskQueue>();
  std::cerr << "in barrier\n";
  // auto dense_data = _dense_matrix_obj_pool.get();
  int count = 100;
  float sum = 0.0;
  for (int i = 1; i <= 100; i++) {
    auto dense_data = std::make_shared<std::vector<float>>();
    auto async_task = new DenseAsyncTask(dense_data, 0, push_timer);
    async_task->data()->resize(count);
    float *data = async_task->data()->data();
    float add = i * 1.0;
    for (int j = 0; j < count; j++) data[j] = add;
    sum += add;
    _push_dense_task_queue_map[0]->push(std::move(async_task));
  }
  ThreadPool<int> async_merge_dense_threads(10);
  int Case = 1;
  int _async_call_num = 0;
  while (Case--) {
    platform::Timer timeline;
    timeline.Start();
    for (auto &task_queue_itr : _push_dense_task_queue_map) {
      auto &task_queue = task_queue_itr.second;
      auto queue_size = task_queue->size();
      ++_async_call_num;
      std::shared_ptr<DenseAsyncTask> task(task_queue->pop());
      CommMergeAccessor t;
      auto *accessor = &t;
      //设置请求回调

      auto &total_send_data_vec = *(task->data());
      float *total_send_data = const_cast<float *>(total_send_data_vec.data());
      size_t total_send_data_size = total_send_data_vec.size();
      {
        CostTimer merge_timer("pslib_downpour_client_push_dense_merge");
        uint32_t merge_count = 0;
        std::vector<std::future<int>> merge_status(merge_size);
        while (!task_queue->empty() && merge_count < merge_size) {
          auto *async_task = task_queue->pop();
          // closure->add_timer(async_task->timer());
          // closure->add_promise(async_task->promise());
          merge_status[merge_count] = async_merge_dense_threads.AddTask(
              [accessor, &total_send_data, total_send_data_size,
               async_task]() -> int {
                auto &tmp_task_vec = *(async_task->data());
                const float *merge_data = tmp_task_vec.data();
                accessor->merge(&total_send_data, &merge_data,
                                total_send_data_size);
#pragma optimize("", off)
                // auto *debug_closure = closure;
                auto *debug_task = async_task;
                delete async_task;
#pragma optimize("", on)
                return 0;
              });
          ++merge_count;
        }
        for (int i = 0; i < merge_count; ++i) {
          merge_status[i].wait();
        }
        for (int i = 0; i < count; i++) std::cerr << total_send_data[i] << " ";
      }
    }
  }
  int emb_dim = 10;
  int trainers = 2;
  bool sync = true;

  TableParameter table_config;
  table_config.set_table_class("BarrierTable");
  FsClientParameter fs_config;
  Table *table = new BarrierTable();
  TableAccessorParameter *accessor_config = table_config.mutable_accessor();
  accessor_config->set_accessor_class("CommMergeAccessor");
  CommonAccessorParameter *common_config = table_config.mutable_common();
  common_config->set_table_name("barrier_table");
  common_config->set_trainer_num(trainers);
  common_config->set_sync(sync);

  auto ret = table->initialize(table_config, fs_config);

  std::unordered_map<uint32_t, std::shared_ptr<Table>> maps =
      std::unordered_map<uint32_t, std::shared_ptr<Table>>();

  table->set_table_map(&maps);

  std::shared_ptr<::ThreadPool> pool_ =
      std::make_shared<::ThreadPool>(trainers);
  std::vector<std::future<void>> task_status;

  for (auto x = 0; x < trainers; x++) {
    auto task = [table, x] { table->barrier(x, 0); };
    task_status.push_back(pool_->enqueue(std::move(task)));
  }

  for (auto &status : task_status) {
    status.wait();
  }

  ASSERT_EQ(ret, 0);
}

}  // namespace distributed
}  // namespace paddle
