-- Copyright 2018 Frank Benkstein
--
-- Licensed under the Apache License, Version 2.0 (the "License");
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
--     http://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

require 'package'
require 'debug'
require 'string'
require 'os'

-- name of this file
local filename = string.sub(debug.getinfo(1, 'S').source, 2)

-- directory this file is in
local dirname = string.gsub(filename, "^(.*/)[^/]*$", "%1")

-- add argparse directory to package search path
package.path = package.path .. ";" .. dirname .. "../ext/lua-argparse/src/argparse.lua"

local argparse = require 'argparse'

local majak_version = "0.0.1"

local function show_version_and_exit()
  print("majak " .. majak_version)
  os.exit(1)
end

local function add_commands(parent_command, subcommands)
  local parser = parent_command.parser

  for command_name, command in pairs(subcommands) do
    command.parser = parser:command(command_name)
    command.fullname = parent_command.fullname .. " " .. command_name

    if command.add_arguments then
      command:add_arguments()
    end

    assert(command.run, "command '" .. command.fullname .. "' has no 'run' function")
  end
end

local function run_subcommand(parser, subcommands, target, args)
  local inspect = require 'inspect'
  local command = subcommands[args[target]]

  if command then
    command:run(args)
  else
    print(parser:get_help())
    os.exit(1)
  end
end

local commands = {
  build = {},
  debug = {},
  version = {run = show_version_and_exit},
}

function commands.build:add_arguments(parser)
  local parser = self.parser
  parser:description "Build the given targets."
  parser:epilog [[
If targets are unspecified, builds the 'default' target (see manual).]]
  parser:option("-j", "run N jobs in parallel [default derived from CPUs available]")
    :argname "N"
    :convert(tonumber)
  parser:option("-k", "keep going until N jobs fail (0 means infinity) [default=1]", 1)
    :argname "N"
    :convert(tonumber)
  parser:flag("-n", "dry run (don't run commands but act like they succeeded)")
  parser:flag("-v", "show all command lines while building")
  parser:argument "targets":args "*"
end

function commands.build:run(args)
  error("majak build not implemented")
end

local debug_commands = {
  ["dump-build-log"] = {}
}

function commands.debug:add_arguments(parser)
  local parser = self.parser
  parser:description "Debugging commands."
  parser:command_target "debug_command"
  parser:require_command(false)
  add_commands(self, debug_commands)
end

function commands.debug:run(args)
  run_subcommand(self.parser, debug_commands, "debug_command", args)
end

debug_commands["dump-build-log"].run = function()
  error("majak debug dump-build-log not implemented")
end

local function main()
  local parser = argparse("majak")
    :description "An alternative interface to the ninja build system."
  local version_description = "Print majak version (" .. majak_version .. ") and exit."
  parser:flag("-V --version", version_description)
    :action(show_version_and_exit)
  parser:option("-C", "Change to DIR before doing anything else.")
    :argname "DIR"

  parser:command_target "command"
  parser:require_command(false)

  add_commands({fullname = "majak", parser = parser}, commands)
  commands.version.parser:description(version_description)
  commands.version.parser:hidden(true)

  local args = parser:parse()

  run_subcommand(parser, commands, "command", args)
end

main()
