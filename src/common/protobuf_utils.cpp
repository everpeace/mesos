/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <process/clock.hpp>
#include <process/pid.hpp>

#include <stout/net.hpp>
#include <stout/stringify.hpp>
#include <stout/uuid.hpp>

#include "common/type_utils.hpp"

#include "messages/messages.hpp"

namespace mesos {
namespace internal {
namespace protobuf {

bool isTerminalState(const TaskState& state)
{
  return (state == TASK_FINISHED ||
          state == TASK_FAILED ||
          state == TASK_KILLED ||
          state == TASK_LOST);
}


// TODO(vinod): Make SlaveID optional because 'StatusUpdate.SlaveID'
// is optional.
StatusUpdate createStatusUpdate(
    const FrameworkID& frameworkId,
    const Option<SlaveID>& slaveId,
    const TaskID& taskId,
    const TaskState& state,
    const std::string& message = "",
    const Option<ExecutorID>& executorId = None())
{
  StatusUpdate update;

  update.set_timestamp(process::Clock::now().secs());
  update.set_uuid(UUID::random().toBytes());
  update.mutable_framework_id()->MergeFrom(frameworkId);

  if (slaveId.isSome()) {
    update.mutable_slave_id()->MergeFrom(slaveId.get());
  }

  if (executorId.isSome()) {
    update.mutable_executor_id()->MergeFrom(executorId.get());
  }

  TaskStatus* status = update.mutable_status();
  status->mutable_task_id()->MergeFrom(taskId);

  if (slaveId.isSome()) {
    status->mutable_slave_id()->MergeFrom(slaveId.get());
  }

  status->set_state(state);
  status->set_message(message);
  status->set_timestamp(update.timestamp());

  return update;
}


Task createTask(const TaskInfo& task,
                       const TaskState& state,
                       const ExecutorID& executorId,
                       const FrameworkID& frameworkId)
{
  Task t;
  t.mutable_framework_id()->MergeFrom(frameworkId);
  t.set_state(state);
  t.set_name(task.name());
  t.mutable_task_id()->MergeFrom(task.task_id());
  t.mutable_slave_id()->MergeFrom(task.slave_id());
  t.mutable_resources()->MergeFrom(task.resources());

  if (!task.has_command()) {
    t.mutable_executor_id()->MergeFrom(executorId);
  }

  return t;
}


MasterInfo createMasterInfo(const process::UPID& pid)
{
  MasterInfo info;
  info.set_id(stringify(pid) + "-" + UUID::random().toString());
  info.set_ip(pid.ip);
  info.set_port(pid.port);
  info.set_pid(pid);

  Try<std::string> hostname = net::getHostname(pid.ip);
  if (hostname.isSome()) {
    info.set_hostname(hostname.get());
  }

  return info;
}


} // namespace protobuf {
} // namespace internal {
} // namespace mesos {
