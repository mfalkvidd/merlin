Given(/^I start naemon$/) do
  # Assume the module exists built in git, if it doesn't, use installed
  merlin_module_path = Dir.pwd + "/.libs/merlin.so"
  if not File.exist?(merlin_module_path) then
    # This path is hardcoded for monitor systems. TODO: make more generic
    merlin_module_path = "/opt/monitor/op5/merlin/merlin.so"
  end
  puts "Using module #{merlin_module_path}"
  steps %Q{
    And I have naemon objects stored in oconf.cfg
    And I have config dir checkresults
    And I have config file naemon.cfg
      """
      cfg_file=oconf.cfg
      query_socket=naemon.qh
      check_result_path=checkresults
      broker_module=#{merlin_module_path} merlin.conf
      event_broker_options=-1
      command_file=naemon.cmd
      object_cache_file=objects.cache
      status_file=/dev/null
      """
    And I start daemon naemon naemon.cfg
    And I have query handler path naemon.qh
  }
end

Given(/^I start merlin$/) do
  step "I start daemon merlind -d merlin.conf"
end

Given(/^I have merlin configured for port (\d+)$/) do |port, nodes|
  push_cmd = "#!/bin/sh\necho \"push $@\" >> config_sync.log"
  fetch_cmd = "#!/bin/sh\necho \"fetch $@\" >> config_sync.log"
  configfile = "
    ipc_socket = test_ipc.sock;

    log_level = debug;
    use_syslog = 0;

    module {
      log_file = merlin.log
    }
    daemon {
      pidfile = merlin.pid;
      log_file = merlin.log
      import_program = /bin/false
      port = #{port};
      object_config {
        push = ./push_cmd
        fetch = ./fetch_cmd
      }
    }
    "
  nodes.hashes.each do |obj|
    configfile += sprintf "\n%s %s {\n", obj["type"], obj["name"]
    configfile += "\taddress = 127.0.0.1\n" # There is no other way in tests
    obj.each do |key, value|
      if key != "type" and key != "name" then
        configfile += "\t#{key} = #{value}\n"
      end
    end
    configfile += "}\n"
  end
  step "I have config file config_sync.log", "" # To make sure push steps work
  step "I have config file push_cmd with permission 777", push_cmd
  step "I have config file fetch_cmd with permission 777", fetch_cmd
  step "I have config file merlin.conf", configfile
end

Given(/^node (.*) have ([a-z]*) hash (.*) at ([\d]+)$/) do |node, type, hash, time|
  hexhash = hash.bytes.map { |b| sprintf("%02x",b) }.join
  steps %Q{
    Given I ask query handler merlin testif set #{type} hash #{node} #{hexhash} #{time}
      | filter_var | filter_val | match_var | match_val |
  }
end
