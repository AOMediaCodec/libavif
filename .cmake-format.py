# -----------------------------
# Options affecting formatting.
# -----------------------------
with section("format"):
  # How wide to allow formatted cmake files
  line_width = 130

  # How many spaces to tab for indent
  tab_size = 4

  # If an argument group contains more than this many sub-groups (parg or kwarg
  # groups) then force it to a vertical layout.
  max_subgroups_hwrap = 3

  # If a positional argument group contains more than this many arguments, then
  # force it to a vertical layout.
  max_pargs_hwrap = 10

  # If a statement is wrapped to more than one line, than dangle the closing
  # parenthesis on its own line.
  dangle_parens = True

# ------------------------------------------------
# Options affecting comment reflow and formatting.
# ------------------------------------------------
with section("markup"):
  # enable comment markup parsing and reflow
  enable_markup = False
