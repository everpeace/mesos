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

#include <map>
#include <vector>

#include <stout/lambda.hpp>
#include <stout/os.hpp>
#include <stout/result.hpp>
#include <stout/strings.hpp>

#include <stout/os/read.hpp>

#include <process/check.hpp>
#include <process/collect.hpp>
#include <process/io.hpp>

#include "common/status_utils.hpp"

#include "docker/docker.hpp"

#include "linux/cgroups.hpp"

#include "slave/containerizer/isolators/cgroups/cpushare.hpp"
#include "slave/containerizer/isolators/cgroups/mem.hpp"

using namespace mesos::internal::slave;

using namespace process;

using std::list;
using std::map;
using std::string;
using std::vector;


template<class T>
static Future<T> failure(
    const string& cmd,
    int status,
    const string& err)
{
  return Failure(
      "Failed to '" + cmd + "': exit status = " +
      WSTRINGIFY(status) + " stderr = " + err);
}


// Asynchronously read stderr from subprocess.
static Future<string> err(const Subprocess& s)
{
  CHECK_SOME(s.err());

  Try<Nothing> nonblock = os::nonblock(s.err().get());
  if (nonblock.isError()) {
    return Failure("Cannot set nonblock for stderr: " + nonblock.error());
  }

  // TODO(tnachen): Although unlikely, it's possible to not capture
  // the caller's failure message if io::read stderr fails. Can
  // chain a callback to at least log.
  return io::read(s.err().get());
}


static Future<Nothing> _checkError(const string& cmd, const Subprocess& s)
{
  Option<int> status = s.status().get();
  if (status.isNone()) {
    return Failure("No status found for '" + cmd + "'");
  }

  if (status.get() != 0) {
    // TODO(tnachen): Consider returning stdout as well.
    return err(s).then(
        lambda::bind(failure<Nothing>, cmd, status.get(), lambda::_1));
  }

  return Nothing();
}


// Returns a failure if no status or non-zero status returned from
// subprocess.
static Future<Nothing> checkError(const string& cmd, const Subprocess& s)
{
  return s.status().then(lambda::bind(_checkError, cmd, s));
}


Try<Docker> Docker::create(const string& path, bool validate)
{
  if (!validate) {
    return Docker(path);
  }

  // Make sure that cgroups are mounted, and at least the 'cpu'
  // subsystem is attached.
  Result<string> hierarchy = cgroups::hierarchy("cpu");

  if (hierarchy.isNone()) {
    return Error("Failed to find a mounted cgroups hierarchy "
                 "for the 'cpu' subsystem, you probably need "
                 "to mount cgroups manually!");
  }

  std::string cmd = path + " info";

  Try<Subprocess> s = subprocess(
      cmd,
      Subprocess::PATH("/dev/null"),
      Subprocess::PATH("/dev/null"),
      Subprocess::PATH("/dev/null"));

  if (s.isError()) {
    return Error(s.error());
  }

  Future<Option<int> > status = s.get().status();

  if (!status.await(Seconds(5))) {
    return Error("Docker info failed with time out");
  } else if (status.isFailed()) {
    return Error("Docker info failed: " + status.failure());
  } else if (!status.get().isSome() || status.get().get() != 0) {
    string msg = "Docker info failed to execute";
    if (status.get().isSome()) {
      msg += ", exited with status (" + WSTRINGIFY(status.get().get()) + ")";
    }
    return Error(msg);
  }

  return Docker(path);
}


Try<Docker::Container> Docker::Container::create(const JSON::Object& json)
{
  map<string, JSON::Value>::const_iterator entry =
    json.values.find("Id");
  if (entry == json.values.end()) {
    return Error("Unable to find Id in container");
  }

  JSON::Value idValue = entry->second;
  if (!idValue.is<JSON::String>()) {
    return Error("Id in container is not a string type");
  }

  string id = idValue.as<JSON::String>().value;

  entry = json.values.find("Name");
  if (entry == json.values.end()) {
    return Error("Unable to find Name in container");
  }

  JSON::Value nameValue = entry->second;
  if (!nameValue.is<JSON::String>()) {
    return Error("Name in container is not string type");
  }

  string name = nameValue.as<JSON::String>().value;

  entry = json.values.find("State");
  if (entry == json.values.end()) {
    return Error("Unable to find State in container");
  }

  JSON::Value stateValue = entry->second;
  if (!stateValue.is<JSON::Object>()) {
    return Error("State in container is not object type");
  }

  entry = stateValue.as<JSON::Object>().values.find("Pid");
  if (entry == json.values.end()) {
    return Error("Unable to find Pid in State");
  }

  // TODO(yifan): Reload operator '=' to reuse the value variable above.
  JSON::Value pidValue = entry->second;
  if (!pidValue.is<JSON::Number>()) {
    return Error("Pid in State is not number type");
  }

  pid_t pid = pid_t(pidValue.as<JSON::Number>().value);

  Option<pid_t> optionalPid;
  if (pid != 0) {
    optionalPid = pid;
  }

  return Docker::Container(id, name, optionalPid);
}


Future<Nothing> Docker::run(
    const string& image,
    const string& command,
    const string& name,
    const Option<mesos::Resources>& resources,
    const Option<map<string, string> >& env) const
{
  string cmd = path + " run -d";

  if (resources.isSome()) {
    // TODO(yifan): Support other resources (e.g. disk, ports).
    Option<double> cpus = resources.get().cpus();
    if (cpus.isSome()) {
      uint64_t cpuShare =
        std::max((uint64_t) (CPU_SHARES_PER_CPU * cpus.get()), MIN_CPU_SHARES);
      cmd += " -c " + stringify(cpuShare);
    }

    Option<Bytes> mem = resources.get().mem();
    if (mem.isSome()) {
      Bytes memLimit = std::max(mem.get(), MIN_MEMORY);
      cmd += " -m " + stringify(memLimit.bytes());
    }
  }

  if (env.isSome()) {
    // TODO(tnachen): Use subprocess with args instead once we can
    // handle splitting command string into args.
    foreachpair (string key, string value, env.get()) {
      key = strings::replace(key, "\"", "\\\"");
      value = strings::replace(value, "\"", "\\\"");
      cmd += " -e \"" + key + "=" + value + "\"";
    }
  }

  cmd += " --net=host --name=" + name + " " + image + " " + command;

  VLOG(1) << "Running " << cmd;

  Try<Subprocess> s = subprocess(
      cmd,
      Subprocess::PATH("/dev/null"),
      Subprocess::PATH("/dev/null"),
      Subprocess::PIPE());

  if (s.isError()) {
    return Failure(s.error());
  }

  return checkError(cmd, s.get());
}


Future<Nothing> Docker::kill(const string& container, bool remove) const
{
  const string cmd = path + " kill " + container;

  VLOG(1) << "Running " << cmd;

  Try<Subprocess> s = subprocess(
      cmd,
      Subprocess::PATH("/dev/null"),
      Subprocess::PATH("/dev/null"),
      Subprocess::PIPE());

  if (s.isError()) {
    return Failure(s.error());
  }

  return s.get().status()
    .then(lambda::bind(
        &Docker::_kill,
        *this,
        container,
        cmd,
        s.get(),
        remove));
}

Future<Nothing> Docker::_kill(
    const Docker& docker,
    const string& container,
    const string& cmd,
    const Subprocess& s,
    bool remove)
{
  Option<int> status = s.status().get();

  if (remove) {
    bool force = !status.isSome() || status.get() != 0;
    return docker.rm(container, force);
  }

  return checkError(cmd, s);
}


Future<Nothing> Docker::rm(
    const string& container,
    bool force) const
{
  const string cmd = path + (force ? " rm -f " : " rm ") + container;

  VLOG(1) << "Running " << cmd;

  Try<Subprocess> s = subprocess(
      cmd,
      Subprocess::PATH("/dev/null"),
      Subprocess::PATH("/dev/null"),
      Subprocess::PIPE());

  if (s.isError()) {
    return Failure(s.error());
  }

  return checkError(cmd, s.get());
}


Future<Docker::Container> Docker::inspect(const string& container) const
{
  const string cmd =  path + " inspect " + container;
  VLOG(1) << "Running " << cmd;

  Try<Subprocess> s = subprocess(
      cmd,
      Subprocess::PATH("/dev/null"),
      Subprocess::PIPE(),
      Subprocess::PIPE());

  if (s.isError()) {
    return Failure(s.error());
  }

  return s.get().status()
    .then(lambda::bind(&Docker::_inspect, cmd, s.get()));
}


Future<Docker::Container> Docker::_inspect(
    const string& cmd,
    const Subprocess& s)
{
  // Check the exit status of 'docker inspect'.
  CHECK_READY(s.status());

  Option<int> status = s.status().get();

  if (!status.isSome()) {
    return Failure("No status found from '" + cmd + "'");
  } else if (status.get() != 0) {
    return err(s).then(
        lambda::bind(
            failure<Docker::Container>,
            cmd,
            status.get(),
            lambda::_1));
  }

  // Read to EOF.
  CHECK_SOME(s.out());
  Try<Nothing> nonblock = os::nonblock(s.out().get());
  if (nonblock.isError()) {
    return Failure("Failed to accept nonblock stdout:" + nonblock.error());
  }
  Future<string> output = io::read(s.out().get());
  return output.then(lambda::bind(&Docker::__inspect, lambda::_1));
}


Future<Docker::Container> Docker::__inspect(const string& output)
{
  Try<JSON::Array> parse = JSON::parse<JSON::Array>(output);

  if (parse.isError()) {
    return Failure("Failed to parse JSON: " + parse.error());
  }

  JSON::Array array = parse.get();
  // Only return if only one container identified with name.
  if (array.values.size() == 1) {
    CHECK(array.values.front().is<JSON::Object>());
    Try<Docker::Container> container =
      Docker::Container::create(array.values.front().as<JSON::Object>());

    if (container.isError()) {
      return Failure("Unable to create container: " + container.error());
    }

    return container.get();
  }

  // TODO(benh): Handle the case where the short container ID was
  // not sufficiently unique and 'array.values.size() > 1'.

  return Failure("Failed to find container");
}


Future<list<Docker::Container> > Docker::ps(
    bool all,
    const Option<string>& prefix) const
{
  string cmd = path + (all ? " ps -a" : " ps");

  VLOG(1) << "Running " << cmd;

  Try<Subprocess> s = subprocess(
      cmd,
      Subprocess::PATH("/dev/null"),
      Subprocess::PIPE(),
      Subprocess::PIPE());

  if (s.isError()) {
    return Failure(s.error());
  }

  return s.get().status()
    .then(lambda::bind(&Docker::_ps, *this, cmd, s.get(), prefix));
}


Future<list<Docker::Container> > Docker::_ps(
    const Docker& docker,
    const string& cmd,
    const Subprocess& s,
    const Option<string>& prefix)
{
  Option<int> status = s.status().get();

  if (!status.isSome()) {
    return Failure("No status found from '" + cmd + "'");
  } else if (status.get() != 0) {
    return err(s).then(
        lambda::bind(
            failure<list<Docker::Container> >,
            cmd,
            status.get(),
            lambda::_1));
  }

  // Read to EOF.
  CHECK_SOME(s.out());
  Try<Nothing> nonblock = os::nonblock(s.out().get());
  if (nonblock.isError()) {
    return Failure("Failed to accept nonblock stdout:" + nonblock.error());
  }
  Future<string> output = io::read(s.out().get());
  return output.then(lambda::bind(&Docker::__ps, docker, prefix, lambda::_1));
}


Future<list<Docker::Container> > Docker::__ps(
    const Docker& docker,
    const Option<string>& prefix,
    const string& output)
{
  vector<string> lines = strings::tokenize(output, "\n");

  // Skip the header.
  CHECK(!lines.empty());
  lines.erase(lines.begin());

  list<Future<Docker::Container> > futures;

  foreach (const string& line, lines) {
    // Inspect the containers that we are interested in depending on
    // whether or not a 'prefix' was specified.
    vector<string> columns = strings::split(strings::trim(line), " ");
    // We expect the name column to be the last column from ps.
    string name = columns[columns.size() - 1];
    if (prefix.isNone()) {
      futures.push_back(docker.inspect(name));
    } else if (strings::startsWith(name, prefix.get())) {
      futures.push_back(docker.inspect(name));
    }
  }

  return collect(futures);
}
