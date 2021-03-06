class MerlinNodeConfig

  def initialize
    @current_config = {
      "notifies" => "yes",
      "binlog_dir" => ".",
      "oconfsplit_dir" => "."
    }
  end

  def set_var(name, value)
    @current_config[name] = value
  end

  def get_var(name)
    res = @current_config[name]
    res
  end
end

Before do
  # Create a global variable, that we can easily access it in the step definitions.
  # However, the variable is intended to be accessed exclusively in the
  # features/step_definitions/naemon_system_config.rb step definition for a
  # neat logical isolation.
  @merlinnodeconfig = MerlinNodeConfig.new
end
