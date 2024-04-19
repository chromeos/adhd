{
  global:
    extern "C" {
      plugin_processor_create;
    };

  # Hide everything else.
  local:
    *;
};
